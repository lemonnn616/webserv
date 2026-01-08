#pragma once

#include <string>
#include <map>
#include <vector>
#include <chrono>
#include <signal.h>
#include <sys/types.h>
#include "core/Client.hpp"
#include "ServerConfig.hpp"
#include "cgi/CgiProcess.hpp"

class EventLoop;
class IHttpHandler;

class CoreServer
{
public:
	struct ListenConfig
	{
		unsigned short port;
		std::size_t serverIndex;
	};

	explicit CoreServer(const std::string& configPath);
	int run();

	const std::vector<int>& getListenFds() const;
	std::map<int,Client>& getClients();
	bool isListenFd(int fd) const;
	std::size_t getServerIndexForListenFd(int fd) const;

	void handleNewConnection(EventLoop& loop,int listenFd);
	void handleClientRead(EventLoop& loop,int fd);
	void handleClientWrite(EventLoop& loop,int fd);
	void closeClient(EventLoop& loop,int fd);

	void checkTimeouts(EventLoop& loop);

	void setHttpHandler(IHttpHandler* handler);
	void setListenConfigs(const std::vector<ListenConfig>& configs);
	const ServerConfig& getServerConfig(std::size_t index) const;
	const std::vector<ServerConfig>& getServerConfigs() const;

	void registerCgiProcess(pid_t pid,int clientFd,int stdinFd,int stdoutFd,int stderrFd,const std::string& stdinData);
	bool isCgiFd(int fd) const;
	void handleCgiRead(EventLoop& loop,int fd);
	void handleCgiWrite(EventLoop& loop,int fd);
	void reapChildren(EventLoop& loop);

	static void handleStopSignal(int signum);
	static bool stopRequested();
	void shutdown(EventLoop& loop);

private:
	std::vector<ServerConfig> _serverConfigs;
	std::string _configPath;
	std::vector<int> _listenFds;
	std::vector<ListenConfig> _listenConfigs;
	std::map<int,std::size_t> _listenFdToServerIndex;
	std::map<int,unsigned short> _listenFdToPort;

	std::map<int,Client> _clients;
	std::map<pid_t,CgiProcess> _cgi;
	std::map<int,pid_t> _cgiFdToPid;
	std::chrono::seconds _cgiTimeout;

	std::chrono::seconds _readTimeout;
	std::chrono::seconds _writeTimeout;
	std::chrono::seconds _idleTimeout;

	IHttpHandler* _httpHandler;

	void cleanupCgi(EventLoop& loop,pid_t pid);
	void checkCgiTimeouts(EventLoop& loop);
	bool initListenSockets();
	int createListenSocket(unsigned short port);

	unsigned short getListenPortForListenFd(int fd) const;
	void updateServerIndexFromHost(Client& client);
	std::size_t selectServerIndexByHost(unsigned short port,std::size_t defaultIndex,const std::string& host) const;

	static volatile sig_atomic_t _stopRequested;

};
