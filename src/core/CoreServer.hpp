#pragma once

#include <string>
#include <map>
#include <vector>
#include <chrono>
#include "core/Client.hpp"

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

private:
	std::string _configPath;
	std::vector<int> _listenFds;
	std::vector<ListenConfig> _listenConfigs;
	std::map<int,std::size_t> _listenFdToServerIndex;
	std::map<int,Client> _clients;

	std::chrono::seconds _readTimeout;
	std::chrono::seconds _writeTimeout;
	std::chrono::seconds _idleTimeout;

	IHttpHandler* _httpHandler;

	bool initListenSockets();
	int createListenSocket(unsigned short port);
};
