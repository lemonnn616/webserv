#pragma once

#include <string>
#include <chrono>
#include <cstddef>
#include <sys/types.h>

struct CgiProcess
{
	pid_t pid;
	int clientFd;

	int stdinFd;
	int stdoutFd;
	int stderrFd;

	std::string stdinBuffer;
	std::size_t stdinOffset;

	std::string stdoutBuffer;
	std::string stderrBuffer;

	std::string method;
	std::string version;

	bool stdinClosed;
	bool stdoutClosed;
	bool stderrClosed;

	bool exited;
	int exitStatus;

	std::chrono::steady_clock::time_point startTime;

	CgiProcess()
		: pid(-1)
		, clientFd(-1)
		, stdinFd(-1)
		, stdoutFd(-1)
		, stderrFd(-1)
		, stdinBuffer()
		, stdinOffset(0)
		, stdoutBuffer()
		, stderrBuffer()
		, method()
		, version()
		, stdinClosed(false)
		, stdoutClosed(false)
		, stderrClosed(false)
		, exited(false)
		, exitStatus(0)
		, startTime(std::chrono::steady_clock::now())
	{
	}
};
