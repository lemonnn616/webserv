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
#include <cctype>
#include <limits>

static const std::size_t MAX_HEADER_BYTES=64*1024;

static std::string makePlainResponse(int status,const std::string& reason,const std::string& body)
{
	std::string res;
	res+="HTTP/1.1 ";
	res+=std::to_string(status);
	res+=" ";
	res+=reason;
	res+="\r\n";
	res+="Connection: close\r\n";
	res+="Content-Type: text/plain\r\n";
	res+="Content-Length: ";
	res+=std::to_string(body.size());
	res+="\r\n";
	res+="\r\n";
	res+=body;
	return res;
}

static void failClose(EventLoop& loop,int fd,Client& client,int status,const std::string& reason,const std::string& body)
{
	std::string().swap(client.inBuffer);

	client.outBuffer=makePlainResponse(status,reason,body);
	client.state=ConnectionState::WRITING;
	client.closeAfterWrite=true;
	client.outOffset=0;

	loop.setReadEnabled(fd,false);
	loop.setWriteEnabled(fd,true);
}

static std::string toLowerStr(const std::string& s)
{
	std::string r=s;
	for(std::size_t i=0;i<r.size();++i)
	{
		r[i]=static_cast<char>(std::tolower(static_cast<unsigned char>(r[i])));
	}
	return r;
}

static std::string ltrimSpaces(const std::string& s)
{
	std::size_t i=0;
	while(i<s.size()&&(s[i]==' '||s[i]=='\t'))
	{
		++i;
	}
	return s.substr(i);
}

static bool parseSizeTStrict(const std::string& s,std::size_t& out)
{
	if(s.empty())
		return false;

	unsigned long long v=0;

	for(std::size_t i=0;i<s.size();++i)
	{
		unsigned char c=static_cast<unsigned char>(s[i]);
		if(!std::isdigit(c))
			return false;

		unsigned int d=(unsigned int)(c-'0');
		if(v>(std::numeric_limits<unsigned long long>::max()-d)/10ULL)
			return false;

		v=v*10ULL+d;
	}

	if(v>(unsigned long long)std::numeric_limits<std::size_t>::max())
		return false;

	out=(std::size_t)v;
	return true;
}

static bool extractContentLength(const std::string& headersBlock,std::size_t& out)
{
	std::size_t lineEnd=headersBlock.find("\r\n");
	if(lineEnd==std::string::npos)
		return false;

	std::size_t pos=lineEnd+2;

	while(pos<headersBlock.size())
	{
		std::size_t end=headersBlock.find("\r\n",pos);
		if(end==std::string::npos)
			break;

		std::string line=headersBlock.substr(pos,end-pos);
		pos=end+2;

		if(line.empty())
			break;

		std::size_t colon=line.find(':');
		if(colon==std::string::npos)
			continue;

		std::string key=toLowerStr(line.substr(0,colon));
		std::string val=ltrimSpaces(line.substr(colon+1));

		if(key=="content-length")
		{
			return parseSizeTStrict(val,out);
		}
	}

	return false;
}

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
				break;

			if(errno==EMFILE||errno==ENFILE)
			{
				Logger::error("accept EMFILE/ENFILE on listen fd "+std::to_string(listenFd));

				if(_reserveFd>=0)
				{
					::close(_reserveFd);
					_reserveFd=-1;
				}

				int tmp=::accept(listenFd,reinterpret_cast<sockaddr*>(&addr),&addrLen);
				if(tmp>=0)
					::close(tmp);

				_reserveFd=::open("/dev/null",O_RDONLY);
				break;
			}

			Logger::error("accept failed");
			break;
		}

		if(_clients.size()>=_maxClients)
		{
			::close(clientFd);
			continue;
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
			client.peerClosed=true;
			Logger::info("Peer EOF on fd "+std::to_string(fd));
			break;
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

	if(client.inBuffer.empty())
	{
		if(client.peerClosed)
		{
			if(client.state==ConnectionState::WRITING || !client.outBuffer.empty())
				return;

			closeClient(loop,fd);
		}
		return;
	}

	std::size_t headersEnd=client.inBuffer.find("\r\n\r\n");

	if(headersEnd==std::string::npos)
	{
		if(client.inBuffer.size()>MAX_HEADER_BYTES)
		{
			failClose(loop,fd,client,431,"Request Header Fields Too Large","Headers too large\n");
		}
	}
	else
	{
		std::size_t headerBytes=headersEnd+4;
		if(headerBytes>MAX_HEADER_BYTES)
		{
			failClose(loop,fd,client,431,"Request Header Fields Too Large","Headers too large\n");
		}
		else
		{
			updateServerIndexFromHost(client);

			std::size_t idx=client.serverConfigIndex;
			if(idx>=_serverConfigs.size())
				idx=0;

			std::size_t maxBody=_serverConfigs[idx].clientMaxBodySize;

			std::size_t contentLength=0;
			std::string headersBlock=client.inBuffer.substr(0,headersEnd);

			if(extractContentLength(headersBlock,contentLength))
			{
				if(contentLength>maxBody)
				{
					failClose(loop,fd,client,413,"Payload Too Large","Payload Too Large\n");
				}
			}

			if(client.state!=ConnectionState::WRITING)
			{
				std::size_t bodyBytes=client.inBuffer.size()-headerBytes;
				if(bodyBytes>maxBody)
				{
					failClose(loop,fd,client,413,"Payload Too Large","Payload Too Large\n");
				}
			}
		}
	}

	if(client.state==ConnectionState::WRITING)
	{
		return;
	}

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
			client.closeAfterWrite=true;
			client.outOffset=0;
			loop.setReadEnabled(fd,false);
			loop.setWriteEnabled(fd,true);
			return;
		}

		if(client.peerClosed && client.state!=ConnectionState::WRITING && client.outBuffer.empty())
		{
			closeClient(loop,fd);
			return;
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

		if(client.closeAfterWrite || client.peerClosed)
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
