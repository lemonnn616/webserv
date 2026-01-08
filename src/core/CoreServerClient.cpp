#include "core/CoreServer.hpp"
#include "core/EventLoop.hpp"
#include "core/Logger.hpp"
#include "http/IHttpHandler.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <vector>

void CoreServer::handleNewConnection(EventLoop& loop,int listenFd)
{
	std::size_t serverIndex=getServerIndexForListenFd(listenFd);
	unsigned short listenPort=getListenPortForListenFd(listenFd);

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
		client.listenPort=listenPort;

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
			updateServerIndexFromHost(client);

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
				client.closeAfterWrite=true;
				client.outOffset=0;
				loop.setReadEnabled(fd,false);
				loop.setWriteEnabled(fd,true);
			}
		}
		else
		{
			client.outBuffer.append(client.inBuffer);
			client.inBuffer.clear();
			client.state=ConnectionState::WRITING;
			client.closeAfterWrite=true;
			client.outOffset=0;
			loop.setReadEnabled(fd,false);
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

	while(client.outOffset<client.outBuffer.size())
	{
		const char* data=client.outBuffer.data()+client.outOffset;
		std::size_t remain=client.outBuffer.size()-client.outOffset;

		ssize_t n=::send(fd,data,remain,0);
		if(n>0)
		{
			client.lastActivity=std::chrono::steady_clock::now();
			client.outOffset+=static_cast<std::size_t>(n);
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
	if(client.outOffset>=client.outBuffer.size())
	{
		client.outBuffer.clear();
		client.outOffset=0;

		if(client.closeAfterWrite)
		{
			closeClient(loop,fd);
			return;
		}

		client.state=ConnectionState::READING;
		loop.setWriteEnabled(fd,false);
		loop.setReadEnabled(fd,true);
	}
}

void CoreServer::closeClient(EventLoop& loop,int fd)
{
	std::vector<pid_t> toKill;

	for(std::map<pid_t,CgiProcess>::iterator it=_cgi.begin();it!=_cgi.end();++it)
	{
		if(it->second.clientFd==fd)
		{
			toKill.push_back(it->first);
		}
	}

	for(std::size_t i=0;i<toKill.size();++i)
	{
		::kill(toKill[i],SIGKILL);
		cleanupCgi(loop,toKill[i]);
	}

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

	checkCgiTimeouts(loop);
	reapChildren(loop);
}
