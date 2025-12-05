#pragma once

#include <vector>
#include <poll.h>

class CoreServer;

class EventLoop
{
public:
	EventLoop();
	void run(CoreServer& server);
	void addClient(int fd);
	void removeFd(int fd);
	void setWriteEnabled(int fd,bool enabled);

private:
	std::vector<struct pollfd> _pollFds;
	void compact();
};
