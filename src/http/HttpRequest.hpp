
#pragma once

#include <string>
#include <map>

struct HttpRequest
{
	std::string method;
	std::string target;
	std::string path;
	std::string query;
	std::string version;
	bool hadTrailingSlash = false;

	
	std::map<std::string, std::string> headers;

	std::string body;
};
