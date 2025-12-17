#include "core/CoreServer.hpp"
#include "core/EventLoop.hpp"
#include "core/Logger.hpp"
#include "http/IHttpHandler.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <vector>

CoreServer::CoreServer(const std::string& configPath)
	:_configPath(configPath)
	,_listenFds()
	,_listenConfigs()
	,_listenFdToServerIndex()
	,_clients()
	,_readTimeout(std::chrono::seconds(30))
	,_writeTimeout(std::chrono::seconds(30))
	,_idleTimeout(std::chrono::seconds(120))
	,_httpHandler(nullptr)
{
	ListenConfig cfg;
	cfg.port=8080;
	cfg.serverIndex=0;
	_listenConfigs.push_back(cfg);
}

int CoreServer::run()
{
	Logger::info("CoreServer starting with config: "+_configPath);

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
			return false;
		}
		_listenFds.push_back(fd);
		_listenFdToServerIndex[fd]=cfg.serverIndex;
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

void CoreServer::handleNewConnection(EventLoop& loop,int listenFd)
{
	std::size_t serverIndex=getServerIndexForListenFd(listenFd);

	while(true)
	{
		sockaddr_in addr;
		socklen_t addrLen=sizeof(addr);
		int clientFd=::accept(listenFd,reinterpret_cast<sockaddr*>(&addr),&addrLen);

		if(clientFd<0)
		{
			if(errno==EAGAIN||errno==EWOULDBLOCK)
			{
				break;
			}
			Logger::error("accept failed");
			break;
		}

		int flags=::fcntl(clientFd,F_GETFL,0);
		if(flags<0||::fcntl(clientFd,F_SETFL,flags|O_NONBLOCK)<0)
		{
			Logger::error("fcntl O_NONBLOCK on client failed");
			::close(clientFd);
			continue;
		}

		Client client;
		client.fd=clientFd;
		client.state=ConnectionState::READING;
		client.lastActivity=std::chrono::steady_clock::now();
		client.serverConfigIndex=serverIndex;

		_clients[clientFd]=client;
		loop.addClient(clientFd);

		Logger::info("New client fd "+std::to_string(clientFd));
	}
}

void CoreServer::handleClientRead(EventLoop& loop,int fd)
{
	std::map<int,Client>::iterator it=_clients.find(fd);
	if(it==_clients.end())
	{
		return;
	}
	Client& client=it->second;

	char buffer[4096];

	while(true)
	{
		ssize_t n=::recv(fd,buffer,sizeof(buffer),0);
		if(n>0)
		{
			client.lastActivity=std::chrono::steady_clock::now();
			client.inBuffer.append(buffer,static_cast<std::size_t>(n));
		}
		else if(n==0)
		{
			Logger::info("Client closed fd "+std::to_string(fd));
			closeClient(loop,fd);
			return;
		}
		else
		{
			if(errno==EAGAIN||errno==EWOULDBLOCK)
			{
				break;
			}
			Logger::error("recv failed on fd "+std::to_string(fd));
			closeClient(loop,fd);
			return;
		}
	}

	if(!client.inBuffer.empty())
	{
		if(_httpHandler!=nullptr)
		{
			_httpHandler->onDataReceived(
				fd,
				client.inBuffer,
				client.outBuffer,
				client.state,
				client.serverConfigIndex,
				client.sessionId
			);

			if(client.state==ConnectionState::CLOSING)
			{
				closeClient(loop,fd);
				return;
			}

			if(client.state==ConnectionState::WRITING&& !client.outBuffer.empty())
			{
				loop.setWriteEnabled(fd,true);
			}
		}
		else
		{
			client.outBuffer.append(client.inBuffer);
			client.inBuffer.clear();
			client.state=ConnectionState::WRITING;
			loop.setWriteEnabled(fd,true);
		}
	}
}

void CoreServer::handleClientWrite(EventLoop& loop,int fd)
{
	std::map<int,Client>::iterator it=_clients.find(fd);
	if(it==_clients.end())
	{
		return;
	}
	Client& client=it->second;

	while(!client.outBuffer.empty())
	{
		ssize_t n=::send(fd,client.outBuffer.data(),client.outBuffer.size(),0);
		if(n>0)
		{
			client.lastActivity=std::chrono::steady_clock::now();
			client.outBuffer.erase(0,static_cast<std::size_t>(n));
		}
		else if(n==0)
		{
			break;
		}
		else
		{
			if(errno==EAGAIN||errno==EWOULDBLOCK)
			{
				break;
			}
			Logger::error("send failed on fd "+std::to_string(fd));
			closeClient(loop,fd);
			return;
		}
	}
	if (client.outBuffer.empty())
		{
			if (client.closeAfterWrite)
			{
				closeClient(loop, fd);
				return;
			}
			client.state = ConnectionState::READING;
			loop.setWriteEnabled(fd, false);
		}
}

void CoreServer::closeClient(EventLoop& loop,int fd)
{
	std::map<int,Client>::iterator it=_clients.find(fd);
	if(it!=_clients.end())
	{
		_clients.erase(it);
	}

	loop.removeFd(fd);
	::close(fd);

	Logger::info("Closed client fd "+std::to_string(fd));
}

void CoreServer::checkTimeouts(EventLoop& loop)
{
	std::chrono::steady_clock::time_point now=std::chrono::steady_clock::now();
	std::vector<int> toClose;
	toClose.reserve(_clients.size());

	for(std::map<int,Client>::iterator it=_clients.begin();it!=_clients.end();++it)
	{
		Client& client=it->second;
		std::chrono::steady_clock::duration idle=now-client.lastActivity;

		if(idle>_idleTimeout)
		{
			Logger::info("Idle timeout on fd "+std::to_string(client.fd));
			toClose.push_back(client.fd);
			continue;
		}

		if(client.state==ConnectionState::READING&& idle>_readTimeout)
		{
			Logger::info("Read timeout on fd "+std::to_string(client.fd));
			toClose.push_back(client.fd);
			continue;
		}

		if(client.state==ConnectionState::WRITING&& idle>_writeTimeout)
		{
			Logger::info("Write timeout on fd "+std::to_string(client.fd));
			toClose.push_back(client.fd);
			continue;
		}
	}

	for(std::size_t i=0;i<toClose.size();++i)
	{
		int fd=toClose[i];
		if(_clients.find(fd)!=_clients.end())
		{
			closeClient(loop,fd);
		}
	}
}
const std::vector<ServerConfig>& CoreServer::getServerConfigs() const
{
	return _serverConfigs;
}

const ServerConfig& CoreServer::getServerConfig(std::size_t index) const
{
	if (index >= _serverConfigs.size())
		return _serverConfigs[0];
	return _serverConfigs[index];
}
