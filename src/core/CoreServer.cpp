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
#include <vector>
#include <sys/wait.h>
#include <signal.h>
#include <set>
#include <stdexcept>
#include <cctype>

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

static std::string extractHostFromHeadersBlock(const std::string& headersBlock)
{
	std::size_t lineEnd=headersBlock.find("\r\n");
	if(lineEnd==std::string::npos)
	{
		return "";
	}

	std::size_t pos=lineEnd+2;

	while(pos<headersBlock.size())
	{
		std::size_t end=headersBlock.find("\r\n",pos);
		if(end==std::string::npos)
		{
			break;
		}

		std::string line=headersBlock.substr(pos,end-pos);
		pos=end+2;

		if(line.empty())
		{
			break;
		}

		std::size_t colon=line.find(':');
		if(colon==std::string::npos)
		{
			continue;
		}

		std::string key=toLowerStr(line.substr(0,colon));
		std::string val=ltrimSpaces(line.substr(colon+1));

		if(key=="host")
		{
			if(!val.empty()&& val[0]=='[')
			{
				std::size_t close=val.find(']');
				if(close!=std::string::npos&& close>1)
				{
					val=val.substr(1,close-1);
				}
			}
			else
			{
				std::size_t p=val.find(':');
				if(p!=std::string::npos)
				{
					val=val.substr(0,p);
				}
			}
			return toLowerStr(val);
		}
	}

	return "";
}

std::size_t CoreServer::selectServerIndexByHost(unsigned short port,std::size_t defaultIndex,const std::string& host) const
{
	std::size_t firstOnPort=defaultIndex;
	bool firstSet=false;

	for(std::size_t i=0;i<_serverConfigs.size();++i)
	{
		const ServerConfig& cfg=_serverConfigs[i];
		if(cfg.listenPort!=port)
		{
			continue;
		}

		if(!firstSet)
		{
			firstOnPort=i;
			firstSet=true;
		}

		for(std::size_t j=0;j<cfg.serverNames.size();++j)
		{
			if(toLowerStr(cfg.serverNames[j])==host)
			{
				return i;
			}
		}
	}

	return firstSet ? firstOnPort : defaultIndex;
}

void CoreServer::updateServerIndexFromHost(Client& client)
{
	std::size_t headersEnd=client.inBuffer.find("\r\n\r\n");
	if(headersEnd==std::string::npos)
	{
		return;
	}

	std::string headersBlock=client.inBuffer.substr(0,headersEnd);
	std::string host=extractHostFromHeadersBlock(headersBlock);
	if(host.empty())
	{
		return;
	}

	std::size_t def=client.serverConfigIndex;
	if(def>=_serverConfigs.size())
	{
		def=0;
	}

	unsigned short port=client.listenPort;
	if(port==0&& def<_serverConfigs.size())
	{
		port=_serverConfigs[def].listenPort;
	}

	client.serverConfigIndex=selectServerIndexByHost(port,def,host);
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

static bool setNonBlockingFd(int fd)
{
	int flags=::fcntl(fd,F_GETFL,0);
	if(flags<0)
	{
		return false;
	}
	if(::fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0)
	{
		return false;
	}
	return true;
}

bool CoreServer::isCgiFd(int fd) const
{
	return (_cgiFdToPid.find(fd)!=_cgiFdToPid.end());
}

void CoreServer::registerCgiProcess(pid_t pid,int clientFd,int stdinFd,int stdoutFd,int stderrFd,const std::string& stdinData)
{
	CgiProcess p;
	p.pid=pid;
	p.clientFd=clientFd;
	p.stdinFd=stdinFd;
	p.stdoutFd=stdoutFd;
	p.stderrFd=stderrFd;
	p.stdinBuffer=stdinData;
	p.stdinOffset=0;
	p.startTime=std::chrono::steady_clock::now();

	if(p.stdinFd>=0)
	{
		setNonBlockingFd(p.stdinFd);
		_cgiFdToPid[p.stdinFd]=pid;
	}
	if(p.stdoutFd>=0)
	{
		setNonBlockingFd(p.stdoutFd);
		_cgiFdToPid[p.stdoutFd]=pid;
	}
	if(p.stderrFd>=0)
	{
		setNonBlockingFd(p.stderrFd);
		_cgiFdToPid[p.stderrFd]=pid;
	}

	_cgi[pid]=p;
}

void CoreServer::handleCgiRead(EventLoop& loop,int fd)
{
	std::map<int,pid_t>::iterator itFd=_cgiFdToPid.find(fd);
	if(itFd==_cgiFdToPid.end())
	{
		return;
	}

	pid_t pid=itFd->second;
	std::map<pid_t,CgiProcess>::iterator it=_cgi.find(pid);
	if(it==_cgi.end())
	{
		return;
	}
	CgiProcess& p=it->second;

	char buf[4096];

	while(true)
	{
		ssize_t n=::read(fd,buf,sizeof(buf));
		if(n>0)
		{
			if(fd==p.stdoutFd)
			{
				p.stdoutBuffer.append(buf,static_cast<std::size_t>(n));
			}
			else if(fd==p.stderrFd)
			{
				p.stderrBuffer.append(buf,static_cast<std::size_t>(n));
			}
		}
		else if(n==0)
		{
			loop.removeFd(fd);
			::close(fd);
			_cgiFdToPid.erase(fd);

			if(fd==p.stdoutFd)
			{
				p.stdoutFd=-1;
				p.stdoutClosed=true;
			}
			else if(fd==p.stderrFd)
			{
				p.stderrFd=-1;
				p.stderrClosed=true;
			}
			break;
		}
		else
		{
			if(errno==EAGAIN||errno==EWOULDBLOCK)
			{
				break;
			}

			loop.removeFd(fd);
			::close(fd);
			_cgiFdToPid.erase(fd);

			if(fd==p.stdoutFd)
			{
				p.stdoutFd=-1;
				p.stdoutClosed=true;
			}
			else if(fd==p.stderrFd)
			{
				p.stderrFd=-1;
				p.stderrClosed=true;
			}
			break;
		}
	}

	if(p.exited&& p.stdinClosed&& p.stdoutClosed&& p.stderrClosed)
	{
		cleanupCgi(loop,pid);
	}
}

void CoreServer::handleCgiWrite(EventLoop& loop,int fd)
{
	std::map<int,pid_t>::iterator itFd=_cgiFdToPid.find(fd);
	if(itFd==_cgiFdToPid.end())
	{
		return;
	}

	pid_t pid=itFd->second;
	std::map<pid_t,CgiProcess>::iterator it=_cgi.find(pid);
	if(it==_cgi.end())
	{
		return;
	}
	CgiProcess& p=it->second;

	if(fd!=p.stdinFd)
	{
		return;
	}

	while(p.stdinOffset<p.stdinBuffer.size())
	{
		const char* data=p.stdinBuffer.data()+p.stdinOffset;
		std::size_t remain=p.stdinBuffer.size()-p.stdinOffset;

		ssize_t n=::write(fd,data,remain);
		if(n>0)
		{
			p.stdinOffset+=static_cast<std::size_t>(n);
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
			break;
		}
	}

	if(p.stdinOffset>=p.stdinBuffer.size())
	{
		loop.setWriteEnabled(fd,false);
		loop.removeFd(fd);
		::close(fd);
		_cgiFdToPid.erase(fd);

		p.stdinFd=-1;
		p.stdinClosed=true;

		p.stdinBuffer.clear();
		p.stdinOffset=0;
	}

	if(p.exited&& p.stdinClosed&& p.stdoutClosed&& p.stderrClosed)
	{
		cleanupCgi(loop,pid);
	}
}

void CoreServer::cleanupCgi(EventLoop& loop,pid_t pid)
{
	std::map<pid_t,CgiProcess>::iterator it=_cgi.find(pid);
	if(it==_cgi.end())
	{
		return;
	}

	CgiProcess& p=it->second;

	if(p.stdinFd>=0)
	{
		loop.removeFd(p.stdinFd);
		::close(p.stdinFd);
		_cgiFdToPid.erase(p.stdinFd);
		p.stdinFd=-1;
	}
	if(p.stdoutFd>=0)
	{
		loop.removeFd(p.stdoutFd);
		::close(p.stdoutFd);
		_cgiFdToPid.erase(p.stdoutFd);
		p.stdoutFd=-1;
	}
	if(p.stderrFd>=0)
	{
		loop.removeFd(p.stderrFd);
		::close(p.stderrFd);
		_cgiFdToPid.erase(p.stderrFd);
		p.stderrFd=-1;
	}

	_cgi.erase(it);
}

void CoreServer::reapChildren(EventLoop& loop)
{
	while(true)
	{
		int status=0;
		pid_t pid=::waitpid(-1,&status,WNOHANG);
		if(pid<=0)
		{
			break;
		}

		std::map<pid_t,CgiProcess>::iterator it=_cgi.find(pid);
		if(it!=_cgi.end())
		{
			CgiProcess& p=it->second;
			p.exited=true;
			p.exitStatus=status;

			if(p.stdinClosed&& p.stdoutClosed&& p.stderrClosed)
			{
				cleanupCgi(loop,pid);
			}
		}
	}
}

void CoreServer::checkCgiTimeouts(EventLoop& loop)
{
	std::chrono::steady_clock::time_point now=std::chrono::steady_clock::now();
	std::vector<pid_t> toKill;

	for(std::map<pid_t,CgiProcess>::iterator it=_cgi.begin();it!=_cgi.end();++it)
	{
		CgiProcess& p=it->second;
		std::chrono::steady_clock::duration d=now-p.startTime;

		if(d>_cgiTimeout)
		{
			toKill.push_back(it->first);
		}
	}

	for(std::size_t i=0;i<toKill.size();++i)
	{
		pid_t pid=toKill[i];
		::kill(pid,SIGKILL);
	}
	reapChildren(loop);
}
