#include "core/EventLoop.hpp"
#include "core/CoreServer.hpp"
#include "core/Logger.hpp"

#include <unistd.h>
#include <cerrno>

EventLoop::EventLoop()
{
}

void EventLoop::run(CoreServer& server)
{
	_pollFds.clear();

	const std::vector<int>& listenFds=server.getListenFds();
	for(std::size_t i=0;i<listenFds.size();++i)
	{
		struct pollfd p;
		p.fd=listenFds[i];
		p.events=POLLIN;
		p.revents=0;
		_pollFds.push_back(p);
	}

	Logger::info("EventLoop started");

	while(true)
	{
		int timeoutMs=1000;
		int ret=::poll(_pollFds.data(),_pollFds.size(),timeoutMs);

		if(ret<0)
		{
			if(errno==EINTR)
			{
				continue;
			}
			Logger::error("poll failed");
			break;
		}

		if(ret==0)
		{
			continue;
		}

		for(std::size_t i=0;i<_pollFds.size();++i)
		{
			struct pollfd& fdInfo=_pollFds[i];

			if(fdInfo.fd<0)
			{
				continue;
			}

			if(fdInfo.revents==0)
			{
				continue;
			}

			int fd=fdInfo.fd;

			if(server.isListenFd(fd))
			{
				if(fdInfo.revents&POLLIN)
				{
					server.handleNewConnection(*this,fd);
				}
				if(fdInfo.revents&(POLLERR|POLLHUP|POLLNVAL))
				{
					Logger::error("Listen socket error");
					return;
				}
			}
			else
			{
				if(fdInfo.revents&(POLLERR|POLLHUP|POLLNVAL))
				{
					server.closeClient(*this,fd);
					continue;
				}

				if(fdInfo.revents&POLLIN)
				{
					server.handleClientRead(*this,fd);
				}

				if(fdInfo.revents&POLLOUT)
				{
					server.handleClientWrite(*this,fd);
				}
			}

			fdInfo.revents=0;
		}

		compact();
	}
}

void EventLoop::addClient(int fd)
{
	struct pollfd p;
	p.fd=fd;
	p.events=POLLIN;
	p.revents=0;
	_pollFds.push_back(p);
}

void EventLoop::removeFd(int fd)
{
	for(std::size_t i=0;i<_pollFds.size();++i)
	{
		if(_pollFds[i].fd==fd)
		{
			_pollFds[i].fd=-1;
			_pollFds[i].events=0;
			_pollFds[i].revents=0;
			break;
		}
	}
}

void EventLoop::setWriteEnabled(int fd,bool enabled)
{
	for(std::size_t i=0;i<_pollFds.size();++i)
	{
		if(_pollFds[i].fd==fd)
		{
			if(enabled)
			{
				_pollFds[i].events=(short)(_pollFds[i].events|POLLOUT);
			}
			else
			{
				_pollFds[i].events=(short)(_pollFds[i].events&~POLLOUT);
			}
			break;
		}
	}
}

void EventLoop::compact()
{
	std::vector<struct pollfd> tmp;
	tmp.reserve(_pollFds.size());

	for(std::size_t i=0;i<_pollFds.size();++i)
	{
		if(_pollFds[i].fd>=0)
		{
			tmp.push_back(_pollFds[i]);
		}
	}

	_pollFds.swap(tmp);
}
