#pragma once

#include <string>
#include <map>
#include <sys/types.h>

struct HttpRequest;

class CgiRunner
{
public:
	struct Result
	{
		int exitCode;
		std::string stdoutData;
		std::string stderrData;

		Result() : exitCode(0), stdoutData(), stderrData() {}
	};

	struct Spawned
	{
		pid_t pid;
		int stdinFd;
		int stdoutFd;
		int stderrFd;

		Spawned() : pid(-1), stdinFd(-1), stdoutFd(-1), stderrFd(-1) {}
	};

	static bool run(
		const std::string& interpreter,
		const std::string& scriptPath,
		const HttpRequest& req,
		const std::map<std::string, std::string>& envExtra,
		Result& out
	);

	static bool spawn(
		const std::string& interpreter,
		const std::string& scriptPath,
		const HttpRequest& req,
		const std::map<std::string, std::string>& envExtra,
		Spawned& out
	);
};
