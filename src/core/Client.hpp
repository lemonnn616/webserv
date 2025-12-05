#pragma once

#include <string>
#include <deque>
#include <chrono>
#include "core/ConnectionState.hpp"

struct Client
{
	int fd;
	ConnectionState state;
	std::string inBuffer;
	std::deque<std::string> writeQueue;
	std::size_t writeOffset;
	std::chrono::steady_clock::time_point lastActivity;
	std::string sessionId;
	std::size_t serverConfigIndex;

	Client();
};
