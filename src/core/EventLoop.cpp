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
	_fdToIndex.clear();

	const std::vector<int>& listenFds=server.getListenFds();
	for(std::size_t i=0;i<listenFds.size();++i)
	{
		addFd(listenFds[i],POLLIN);
	}

	Logger::info("EventLoop started");

	while(!CoreServer::stopRequested())
	{
		int timeoutMs=1000;
		int ret=::poll(_pollFds.data(),_pollFds.size(),timeoutMs);

		if(ret<0)
		{
			if(errno==EINTR)
			{
				if(CoreServer::stopRequested())
				{
					break;
				}
				continue;
			}
			Logger::error("poll failed");
			break;
		}

		bool fatal=false;

		if(ret>0)
		{
			std::size_t polledCount=_pollFds.size();

			for(std::size_t i=0;i<polledCount;++i)
			{
				int fd=_pollFds[i].fd;
				short revents=_pollFds[i].revents;
				_pollFds[i].revents=0;

				if(fd<0)
				{
					continue;
				}
				if(revents==0)
				{
					continue;
				}
				if(server.isListenFd(fd))
				{
					if(revents&POLLIN)
					{
						server.handleNewConnection(*this,fd);
					}
					if(revents&(POLLERR|POLLHUP|POLLNVAL))
					{
						Logger::error("Listen socket error");
						fatal=true;
						break;
					}
				}
				else if(server.isCgiFd(fd))
				{
					if(revents&(POLLERR|POLLHUP|POLLNVAL))
					{
						server.handleCgiRead(*this,fd);
						continue;
					}

					if(revents&POLLIN)
					{
						server.handleCgiRead(*this,fd);
					}

					if(revents&POLLOUT)
					{
						server.handleCgiWrite(*this,fd);
					}
				}
				else
				{
					if(revents&(POLLERR|POLLNVAL))
					{
						server.closeClient(*this,fd);
						continue;
					}

					bool hup=(revents&POLLHUP)!=0;

					if((revents&POLLIN) || hup)
					{
						server.handleClientRead(*this,fd);

						if(server.getClients().find(fd)==server.getClients().end())
							continue;
					}

					if(revents&POLLOUT)
					{
						server.handleClientWrite(*this,fd);
					}
				}
			}

			compact();
		}

		if(fatal)
		{
			break;
		}

		server.checkTimeouts(*this);
	}

	server.shutdown(*this);
}

void EventLoop::addFd(int fd,short events)
{
	std::map<int,std::size_t>::iterator it=_fdToIndex.find(fd);
	if(it!=_fdToIndex.end())
	{
		std::size_t idx=it->second;
		_pollFds[idx].events=events;
		_pollFds[idx].revents=0;
		return;
	}

	struct pollfd p;
	p.fd=fd;
	p.events=events;
	p.revents=0;

	_fdToIndex[fd]=_pollFds.size();
	_pollFds.push_back(p);
}

void EventLoop::addClient(int fd)
{
	addFd(fd,POLLIN);
}

void EventLoop::removeFd(int fd)
{
	std::map<int,std::size_t>::iterator it=_fdToIndex.find(fd);
	if(it==_fdToIndex.end())
	{
		return;
	}

	std::size_t idx=it->second;

	_pollFds[idx].fd=-1;
	_pollFds[idx].events=0;
	_pollFds[idx].revents=0;

	_fdToIndex.erase(it);
}

void EventLoop::setWriteEnabled(int fd,bool enabled)
{
	std::map<int,std::size_t>::iterator it=_fdToIndex.find(fd);
	if(it==_fdToIndex.end())
	{
		return;
	}

	std::size_t idx=it->second;

	if(enabled)
	{
		_pollFds[idx].events=(short)(_pollFds[idx].events|POLLOUT);
	}
	else
	{
		_pollFds[idx].events=(short)(_pollFds[idx].events&~POLLOUT);
	}
}

void EventLoop::setReadEnabled(int fd,bool enabled)
{
	std::map<int,std::size_t>::iterator it=_fdToIndex.find(fd);
	if(it==_fdToIndex.end())
	{
		return;
	}

	std::size_t idx=it->second;

	if(enabled)
	{
		_pollFds[idx].events=(short)(_pollFds[idx].events|POLLIN);
	}
	else
	{
		_pollFds[idx].events=(short)(_pollFds[idx].events&~POLLIN);
	}
}

void EventLoop::compact()
{
	std::vector<struct pollfd> tmp;
	tmp.reserve(_pollFds.size());

	_fdToIndex.clear();

	for(std::size_t i=0;i<_pollFds.size();++i)
	{
		if(_pollFds[i].fd>=0)
		{
			_fdToIndex[_pollFds[i].fd]=tmp.size();
			tmp.push_back(_pollFds[i]);
		}
	}

	_pollFds.swap(tmp);
}
