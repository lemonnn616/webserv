#include "core/CoreServer.hpp"
#include "core/EventLoop.hpp"
#include "core/Logger.hpp"
#include "http/IHttpHandler.hpp"
#include "ConfigParser.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <set>
#include <stdexcept>

CoreServer::CoreServer(const std::string& configPath)
	:_serverConfigs()
	,_configPath(configPath)
	,_listenFds()
	,_listenConfigs()
	,_listenFdToServerIndex()
	,_listenFdToPort()
	,_clients()
	,_cgi()
	,_cgiFdToPid()
	,_cgiTimeout(std::chrono::seconds(30))
	,_readTimeout(std::chrono::seconds(30))
	,_writeTimeout(std::chrono::seconds(30))
	,_idleTimeout(std::chrono::seconds(120))
	,_httpHandler(nullptr)
{
	ConfigParser parser;
	if(!parser.parseFile(_configPath,_serverConfigs))
	{
		throw std::runtime_error("Bad config: "+_configPath);
	}

	if(_serverConfigs.empty())
	{
		_serverConfigs.push_back(ServerConfig());
	}

	_listenConfigs.clear();

	std::set<unsigned short> ports;
	for(std::size_t i=0;i<_serverConfigs.size();++i)
	{
		unsigned short port=_serverConfigs[i].listenPort;
		if(ports.insert(port).second)
		{
			ListenConfig cfg;
			cfg.port=port;
			cfg.serverIndex=i;
			_listenConfigs.push_back(cfg);
		}
	}

	if(_listenConfigs.empty())
	{
		ListenConfig cfg;
		cfg.port=8080;
		cfg.serverIndex=0;
		_listenConfigs.push_back(cfg);
	}
}

int CoreServer::run()
{
	Logger::info("CoreServer starting with config: "+_configPath);

	_stopRequested=0;

	struct sigaction sa;
	std::memset(&sa,0,sizeof(sa));
	sa.sa_handler=CoreServer::handleStopSignal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags=0;
	::sigaction(SIGINT,&sa,0);
	::sigaction(SIGTERM,&sa,0);

	struct sigaction sp;
	std::memset(&sp,0,sizeof(sp));
	sp.sa_handler=SIG_IGN;
	sigemptyset(&sp.sa_mask);
	sp.sa_flags=0;
	::sigaction(SIGPIPE,&sp,0);

	if(!initListenSockets())
	{
		return 1;
	}

	EventLoop loop;
	loop.run(*this);
	return 0;
}

void CoreServer::setHttpHandler(IHttpHandler* handler)
{
	_httpHandler=handler;
}

void CoreServer::setListenConfigs(const std::vector<ListenConfig>& configs)
{
	_listenConfigs=configs;
}

bool CoreServer::initListenSockets()
{
	for(std::size_t i=0;i<_listenConfigs.size();++i)
	{
		const ListenConfig& cfg=_listenConfigs[i];
		int fd=createListenSocket(cfg.port);
		if(fd<0)
		{
			Logger::error("Failed to create listen socket on port "+std::to_string(cfg.port));
			for(std::size_t j=0;j<_listenFds.size();++j)
			{
				::close(_listenFds[j]);
			}
			_listenFds.clear();
			_listenFdToServerIndex.clear();
			_listenFdToPort.clear();
			return false;
		}
		_listenFds.push_back(fd);
		_listenFdToServerIndex[fd]=cfg.serverIndex;
		_listenFdToPort[fd]=cfg.port;
		Logger::info("Listening on port "+std::to_string(cfg.port));
	}
	return true;
}

int CoreServer::createListenSocket(unsigned short port)
{
	int fd=::socket(AF_INET,SOCK_STREAM,0);
	if(fd<0)
	{
		Logger::error("socket failed");
		return -1;
	}

	int opt=1;
	if(::setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt))<0)
	{
		Logger::error("setsockopt SO_REUSEADDR failed");
		::close(fd);
		return -1;
	}

	int flags=::fcntl(fd,F_GETFL,0);
	if(flags<0)
	{
		Logger::error("fcntl F_GETFL failed");
		::close(fd);
		return -1;
	}
	if(::fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0)
	{
		Logger::error("fcntl F_SETFL O_NONBLOCK failed");
		::close(fd);
		return -1;
	}

	sockaddr_in addr;
	std::memset(&addr,0,sizeof(addr));
	addr.sin_family=AF_INET;
	addr.sin_addr.s_addr=htonl(INADDR_ANY);
	addr.sin_port=htons(port);

	if(::bind(fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))<0)
	{
		Logger::error("bind failed");
		::close(fd);
		return -1;
	}

	if(::listen(fd,SOMAXCONN)<0)
	{
		Logger::error("listen failed");
		::close(fd);
		return -1;
	}

	return fd;
}

const std::vector<int>& CoreServer::getListenFds() const
{
	return _listenFds;
}

std::map<int,Client>& CoreServer::getClients()
{
	return _clients;
}

bool CoreServer::isListenFd(int fd) const
{
	for(std::size_t i=0;i<_listenFds.size();++i)
	{
		if(_listenFds[i]==fd)
		{
			return true;
		}
	}
	return false;
}

std::size_t CoreServer::getServerIndexForListenFd(int fd) const
{
	std::map<int,std::size_t>::const_iterator it=_listenFdToServerIndex.find(fd);
	if(it!=_listenFdToServerIndex.end())
	{
		return it->second;
	}
	return 0;
}

unsigned short CoreServer::getListenPortForListenFd(int fd) const
{
	std::map<int,unsigned short>::const_iterator it=_listenFdToPort.find(fd);
	if(it!=_listenFdToPort.end())
	{
		return it->second;
	}
	return 0;
}

const std::vector<ServerConfig>& CoreServer::getServerConfigs() const
{
	return _serverConfigs;
}

const ServerConfig& CoreServer::getServerConfig(std::size_t index) const
{
	if(index>=_serverConfigs.size())
		return _serverConfigs[0];
	return _serverConfigs[index];
}
