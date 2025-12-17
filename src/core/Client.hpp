#pragma once

#include <string>
#include <chrono>
#include "core/ConnectionState.hpp"

struct Client
{
	int fd;
	ConnectionState state;
	std::string inBuffer;
	std::string outBuffer;
	std::chrono::steady_clock::time_point lastActivity;

	std::string sessionId;
	std::size_t serverConfigIndex;

	bool closeAfterWrite;

	Client();
};
