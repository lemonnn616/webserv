#include "cgi/CgiRunner.hpp"
#include "http/HttpRequest.hpp"

#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <vector>
#include <cctype>
#include <limits.h>
#include <cstdlib>

static void closeIfValid(int& fd)
{
	if (fd >= 0)
	{
		::close(fd);
		fd = -1;
	}
}

static std::string stripSpaces(const std::string& s)
{
	std::size_t i = 0;
	while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
		++i;

	std::size_t j = s.size();
	while (j > i && (s[j - 1] == ' ' || s[j - 1] == '\t'))
		--j;

	return s.substr(i, j - i);
}

static std::string getHeaderValueLowerKey(
	const std::map<std::string, std::string>& headers,
	const std::string& lowerKey
)
{
	std::map<std::string, std::string>::const_iterator it = headers.find(lowerKey);
	if (it == headers.end())
		return "";
	return it->second;
}

static std::string toUpperHttpKey(const std::string& lowerKey)
{
	std::string r = "HTTP_";
	for (std::size_t i = 0; i < lowerKey.size(); ++i)
	{
		unsigned char c = static_cast<unsigned char>(lowerKey[i]);
		if (c == '-')
			r.push_back('_');
		else
			r.push_back(static_cast<char>(std::toupper(c)));
	}
	return r;
}

static std::vector<char*> buildEnvp(const std::vector<std::string>& env)
{
	std::vector<char*> envp;
	envp.reserve(env.size() + 1);

	for (std::size_t i = 0; i < env.size(); ++i)
		envp.push_back(const_cast<char*>(env[i].c_str()));

	envp.push_back(0);
	return envp;
}

static bool readAllFd(int fd, std::string& out)
{
	char buf[4096];

	while (true)
	{
		ssize_t n = ::read(fd, buf, sizeof(buf));
		if (n > 0)
		{
			out.append(buf, static_cast<std::size_t>(n));
			continue;
		}
		if (n == 0)
			return true;

		return false;
	}
}

static void buildCgiEnv(
	const std::string& scriptPath,
	const HttpRequest& req,
	const std::map<std::string, std::string>& envExtra,
	std::vector<std::string>& envOut
)
{
	envOut.clear();
	envOut.reserve(64);

	envOut.push_back("GATEWAY_INTERFACE=CGI/1.1");
	envOut.push_back("SERVER_PROTOCOL=" + req.version);
	envOut.push_back("REQUEST_METHOD=" + req.method);

	envOut.push_back("SCRIPT_FILENAME=" + scriptPath);
	envOut.push_back("SCRIPT_NAME=" + req.path);
	envOut.push_back("QUERY_STRING=" + req.query);

	{
		std::string host = getHeaderValueLowerKey(req.headers, "host");
		if (!host.empty())
			envOut.push_back("HTTP_HOST=" + stripSpaces(host));
	}
	{
		std::string ct = getHeaderValueLowerKey(req.headers, "content-type");
		if (!ct.empty())
			envOut.push_back("CONTENT_TYPE=" + stripSpaces(ct));
	}

	if (req.method == "POST")
		envOut.push_back("CONTENT_LENGTH=" + std::to_string(req.body.size()));
	else
		envOut.push_back("CONTENT_LENGTH=0");

	for (std::map<std::string, std::string>::const_iterator it = req.headers.begin();
		 it != req.headers.end(); ++it)
	{
		const std::string& k = it->first;
		const std::string& v = it->second;

		if (k == "host" || k == "content-type" || k == "content-length")
			continue;

		envOut.push_back(toUpperHttpKey(k) + "=" + stripSpaces(v));
	}

	for (std::map<std::string, std::string>::const_iterator it = envExtra.begin();
		 it != envExtra.end(); ++it)
	{
		envOut.push_back(it->first + "=" + it->second);
	}
}

bool CgiRunner::spawn(
	const std::string& interpreter,
	const std::string& scriptPath,
	const HttpRequest& req,
	const std::map<std::string, std::string>& envExtra,
	Spawned& out
)
{
	int inPipe[2] = {-1, -1};
	int outPipe[2] = {-1, -1};
	int errPipe[2] = {-1, -1};

	out.pid = -1;
	out.stdinFd = -1;
	out.stdoutFd = -1;
	out.stderrFd = -1;

	if (::pipe(inPipe) < 0)
		return false;
	if (::pipe(outPipe) < 0)
	{
		closeIfValid(inPipe[0]);
		closeIfValid(inPipe[1]);
		return false;
	}
	if (::pipe(errPipe) < 0)
	{
		closeIfValid(inPipe[0]);
		closeIfValid(inPipe[1]);
		closeIfValid(outPipe[0]);
		closeIfValid(outPipe[1]);
		return false;
	}

	pid_t pid = ::fork();
	if (pid < 0)
	{
		closeIfValid(inPipe[0]);
		closeIfValid(inPipe[1]);
		closeIfValid(outPipe[0]);
		closeIfValid(outPipe[1]);
		closeIfValid(errPipe[0]);
		closeIfValid(errPipe[1]);
		return false;
	}

	if (pid == 0)
	{
		::dup2(inPipe[0], STDIN_FILENO);
		::dup2(outPipe[1], STDOUT_FILENO);
		::dup2(errPipe[1], STDERR_FILENO);

		::close(inPipe[0]);
		::close(inPipe[1]);
		::close(outPipe[0]);
		::close(outPipe[1]);
		::close(errPipe[0]);
		::close(errPipe[1]);
		
		std::string absScriptPath;
		{
			char* rp = ::realpath(scriptPath.c_str(), NULL);
			if (rp)
			{
				absScriptPath = rp;
				free(rp);
			}
			else
			{
				if (!scriptPath.empty() && scriptPath[0] == '/')
					absScriptPath = scriptPath;
				else
				{
					char cwd[PATH_MAX];
					if (::getcwd(cwd, sizeof(cwd)))
						absScriptPath = std::string(cwd) + "/" + scriptPath;
					else
						absScriptPath = scriptPath; // fallback
				}
			}
		}

		std::vector<std::string> env;
		buildCgiEnv(absScriptPath, req, envExtra, env);

		std::vector<char*> envp = buildEnvp(env);

		char* argv[3];
		argv[0] = const_cast<char*>(interpreter.c_str());
		argv[1] = const_cast<char*>(absScriptPath.c_str());
		argv[2] = 0;
		{
			std::string dir = absScriptPath;
			size_t pos = dir.rfind('/');
			if (pos != std::string::npos)
				dir = dir.substr(0, pos);
			else
				dir = ".";
			if (::chdir(dir.c_str()) != 0)
			{
				::dprintf(STDERR_FILENO, "chdir failed: %s\n", ::strerror(errno));
				::_exit(127);
			}
		}

		::execve(argv[0], argv, envp.data());
		::_exit(127);
	}

	closeIfValid(inPipe[0]);
	closeIfValid(outPipe[1]);
	closeIfValid(errPipe[1]);

	out.pid = pid;
	out.stdinFd = inPipe[1];
	out.stdoutFd = outPipe[0];
	out.stderrFd = errPipe[0];

	return true;
}
