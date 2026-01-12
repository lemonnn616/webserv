#pragma once

#include <vector>
#include <map>
#include <poll.h>

class CoreServer;

class EventLoop
{
public:
	EventLoop();
	void run(CoreServer& server);
	void addFd(int fd,short events);
	void addClient(int fd);
	void removeFd(int fd);
	void setWriteEnabled(int fd,bool enabled);
	void setReadEnabled(int fd,bool enabled);

private:
	std::vector<struct pollfd> _pollFds;
	std::map<int,std::size_t> _fdToIndex;

	void compact();
};
