#include "core/CoreServer.hpp"
#include "core/EventLoop.hpp"

#include <unistd.h>
#include <vector>
#include <signal.h>

volatile sig_atomic_t CoreServer::_stopRequested=0;

void CoreServer::handleStopSignal(int signum)
{
	(void)signum;
	_stopRequested=1;
}

bool CoreServer::stopRequested()
{
	return (_stopRequested!=0);
}

void CoreServer::shutdown(EventLoop& loop)
{
	if(_reserveFd>=0)
	{
		::close(_reserveFd);
		_reserveFd=-1;
	}

	std::vector<pid_t> pids;
	pids.reserve(_cgi.size());

	for(std::map<pid_t,CgiProcess>::iterator it=_cgi.begin();it!=_cgi.end();++it)
	{
		pids.push_back(it->first);
	}

	for(std::size_t i=0;i<pids.size();++i)
	{
		::kill(pids[i],SIGKILL);
		cleanupCgi(loop,pids[i]);
	}

	reapChildren(loop);

	std::vector<int> clientFds;
	clientFds.reserve(_clients.size());

	for(std::map<int,Client>::iterator it=_clients.begin();it!=_clients.end();++it)
	{
		clientFds.push_back(it->first);
	}

	for(std::size_t i=0;i<clientFds.size();++i)
	{
		if(_clients.find(clientFds[i])!=_clients.end())
		{
			closeClient(loop,clientFds[i]);
		}
	}

	for(std::size_t i=0;i<_listenFds.size();++i)
	{
		loop.removeFd(_listenFds[i]);
		::close(_listenFds[i]);
	}
	_listenFds.clear();
	_listenFdToServerIndex.clear();
	_listenFdToPort.clear();
}
