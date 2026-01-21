#pragma once

#include <string>
#include <map>

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

	// interpreter: "/usr/bin/python3"
	// scriptPath: "www/cgi/test.py"
	// envExtra: дополнительные CGI/HTTP переменные
	static bool run(
		const std::string& interpreter,
		const std::string& scriptPath,
		const HttpRequest& req,
		const std::map<std::string, std::string>& envExtra,
		Result& out
	);
};