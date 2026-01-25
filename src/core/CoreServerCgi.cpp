#include "core/CoreServer.hpp"
#include "core/EventLoop.hpp"
#include "core/Logger.hpp"
#include "http/CgiResponseParser.hpp"
#include "http/HttpError.hpp"
#include "http/HttpResponse.hpp"

#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <vector>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>

static bool setNonBlockingFd(int fd)
{
	int flags = ::fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		return false;
	if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
		return false;
	return true;
}

bool CoreServer::isCgiFd(int fd) const
{
	return (_cgiFdToPid.find(fd) != _cgiFdToPid.end());
}

void CoreServer::registerCgiProcess(
	pid_t pid, int clientFd,
	int stdinFd, int stdoutFd, int stderrFd,
	const std::string& stdinData
)
{
	CgiProcess p;
	p.pid = pid;
	p.clientFd = clientFd;
	p.stdinFd = stdinFd;
	p.stdoutFd = stdoutFd;
	p.stderrFd = stderrFd;

	p.stdinClosed = (p.stdinFd < 0);
	p.stdoutClosed = (p.stdoutFd < 0);
	p.stderrClosed = (p.stderrFd < 0);
	p.stdinBuffer = stdinData;
	p.stdinOffset = 0;
	p.startTime = std::chrono::steady_clock::now();

	if (p.stdinFd >= 0)
	{
		setNonBlockingFd(p.stdinFd);
		_cgiFdToPid[p.stdinFd] = pid;
	}
	if (p.stdoutFd >= 0)
	{
		setNonBlockingFd(p.stdoutFd);
		_cgiFdToPid[p.stdoutFd] = pid;
	}
	if (p.stderrFd >= 0)
	{
		setNonBlockingFd(p.stderrFd);
		_cgiFdToPid[p.stderrFd] = pid;
	}

	_cgi[pid] = p;
}

void CoreServer::finalizeCgiIfDone(EventLoop& loop, pid_t pid)
{
	std::map<pid_t, CgiProcess>::iterator it = _cgi.find(pid);
	if (it == _cgi.end())
		return;

	CgiProcess& p = it->second;

	if (!(p.exited && p.stdinClosed && p.stdoutClosed && p.stderrClosed))
		return;

	int clientFd = p.clientFd;

	std::map<int, Client>::iterator itCl = _clients.find(clientFd);
	if (itCl == _clients.end())
	{
		cleanupCgi(loop, pid);
		return;
	}

	Client& client = itCl->second;

	if (!p.stderrBuffer.empty())
	{
		Logger::warn("CGI stderr (pid " + std::to_string((long long)pid) + "): " + p.stderrBuffer);
	}

	HttpResponse res;

	// БЕЗ тернарника:
	if (!p.version.empty())
		res.version = p.version;
	else
		res.version = "HTTP/1.1";

	if (p.stdoutBuffer.empty())
	{
		HttpError::fill(res, getServerConfig(client.serverConfigIndex), 502, "Bad Gateway");
		res.headers["Connection"] = "close";
	}
	else
	{
		if (!CgiResponseParser::parse(p.stdoutBuffer, res))
		{
			HttpError::fill(res, getServerConfig(client.serverConfigIndex), 502, "Bad Gateway");
			res.headers["Connection"] = "close";
		}
		else
		{
			res.headers["Connection"] = "close";

			// HEAD: тело не отправляем
			if (p.method == "HEAD")
				res.body.clear();
		}
	}

	client.outBuffer = res.serialize();
	client.state = ConnectionState::WRITING;
	client.closeAfterWrite = true;
	client.outOffset = 0;

	loop.setReadEnabled(clientFd, false);
	loop.setWriteEnabled(clientFd, true);

	cleanupCgi(loop, pid);
}

void CoreServer::handleCgiRead(EventLoop& loop, int fd)
{
	std::map<int, pid_t>::iterator itFd = _cgiFdToPid.find(fd);
	if (itFd == _cgiFdToPid.end())
		return;

	pid_t pid = itFd->second;
	std::map<pid_t, CgiProcess>::iterator it = _cgi.find(pid);
	if (it == _cgi.end())
		return;

	CgiProcess& p = it->second;

	char buf[4096];

	while (true)
	{
		ssize_t n = ::read(fd, buf, sizeof(buf));
		if (n > 0)
		{
			if (fd == p.stdoutFd)
				p.stdoutBuffer.append(buf, static_cast<std::size_t>(n));
			else if (fd == p.stderrFd)
				p.stderrBuffer.append(buf, static_cast<std::size_t>(n));
		}
		else if (n == 0)
		{
			loop.removeFd(fd);
			::close(fd);
			_cgiFdToPid.erase(fd);

			if (fd == p.stdoutFd)
			{
				p.stdoutFd = -1;
				p.stdoutClosed = true;
			}
			else if (fd == p.stderrFd)
			{
				p.stderrFd = -1;
				p.stderrClosed = true;
			}
			break;
		}
		else
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;

			loop.removeFd(fd);
			::close(fd);
			_cgiFdToPid.erase(fd);

			if (fd == p.stdoutFd)
			{
				p.stdoutFd = -1;
				p.stdoutClosed = true;
			}
			else if (fd == p.stderrFd)
			{
				p.stderrFd = -1;
				p.stderrClosed = true;
			}
			break;
		}
	}

	finalizeCgiIfDone(loop, pid);
}

void CoreServer::handleCgiWrite(EventLoop& loop, int fd)
{
	std::map<int, pid_t>::iterator itFd = _cgiFdToPid.find(fd);
	if (itFd == _cgiFdToPid.end())
		return;

	pid_t pid = itFd->second;
	std::map<pid_t, CgiProcess>::iterator it = _cgi.find(pid);
	if (it == _cgi.end())
		return;

	CgiProcess& p = it->second;

	if (fd != p.stdinFd)
		return;

	while (p.stdinOffset < p.stdinBuffer.size())
	{
		const char* data = p.stdinBuffer.data() + p.stdinOffset;
		std::size_t remain = p.stdinBuffer.size() - p.stdinOffset;

		ssize_t n = ::write(fd, data, remain);
		if (n > 0)
			p.stdinOffset += static_cast<std::size_t>(n);
		else if (n == 0)
			break;
		else
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			loop.setWriteEnabled(fd, false);
			loop.removeFd(fd);
			::close(fd);
			_cgiFdToPid.erase(fd);

			p.stdinFd = -1;
			p.stdinClosed = true;

			p.stdinBuffer.clear();
			p.stdinOffset = 0;

			break;
		}
	}

	if (p.stdinOffset >= p.stdinBuffer.size())
	{
		loop.setWriteEnabled(fd, false);
		loop.removeFd(fd);
		::close(fd);
		_cgiFdToPid.erase(fd);

		p.stdinFd = -1;
		p.stdinClosed = true;

		p.stdinBuffer.clear();
		p.stdinOffset = 0;
	}

	finalizeCgiIfDone(loop, pid);
}

void CoreServer::cleanupCgi(EventLoop& loop, pid_t pid)
{
	std::map<pid_t, CgiProcess>::iterator it = _cgi.find(pid);
	if (it == _cgi.end())
		return;

	CgiProcess& p = it->second;

	if (p.stdinFd >= 0)
	{
		loop.removeFd(p.stdinFd);
		::close(p.stdinFd);
		_cgiFdToPid.erase(p.stdinFd);
		p.stdinFd = -1;
	}
	if (p.stdoutFd >= 0)
	{
		loop.removeFd(p.stdoutFd);
		::close(p.stdoutFd);
		_cgiFdToPid.erase(p.stdoutFd);
		p.stdoutFd = -1;
	}
	if (p.stderrFd >= 0)
	{
		loop.removeFd(p.stderrFd);
		::close(p.stderrFd);
		_cgiFdToPid.erase(p.stderrFd);
		p.stderrFd = -1;
	}

	_cgi.erase(it);
}

void CoreServer::reapChildren(EventLoop& loop)
{
	while (true)
	{
		int status = 0;
		pid_t pid = ::waitpid(-1, &status, WNOHANG);
		if (pid <= 0)
			break;

		std::map<pid_t, CgiProcess>::iterator it = _cgi.find(pid);
		if (it != _cgi.end())
		{
			CgiProcess& p = it->second;
			p.exited = true;
			p.exitStatus = status;

			finalizeCgiIfDone(loop, pid);
		}
	}
}

void CoreServer::checkCgiTimeouts(EventLoop& loop)
{
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	std::vector<pid_t> toKill;

	for (std::map<pid_t, CgiProcess>::iterator it = _cgi.begin(); it != _cgi.end(); ++it)
	{
		CgiProcess& p = it->second;
		std::chrono::steady_clock::duration d = now - p.startTime;

		if (d > _cgiTimeout)
			toKill.push_back(it->first);
	}

	for (std::size_t i = 0; i < toKill.size(); ++i)
		::kill(toKill[i], SIGKILL);

	reapChildren(loop);
}

void CoreServer::registerCgiProcess(
	EventLoop& loop, pid_t pid, int clientFd,
	int stdinFd, int stdoutFd, int stderrFd,
	const std::string& stdinData
)
{
	registerCgiProcess(pid, clientFd, stdinFd, stdoutFd, stderrFd, stdinData);

	if (stdinFd >= 0)
		loop.addFd(stdinFd, POLLOUT);
	if (stdoutFd >= 0)
		loop.addFd(stdoutFd, POLLIN);
	if (stderrFd >= 0)
		loop.addFd(stderrFd, POLLIN);
}
