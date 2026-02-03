/* src/http/HttpRequest.hpp */
#pragma once

#include <string>
#include <map>

struct HttpRequest
{
	std::string method;
	std::string target;// raw target from request line (before parsing)
	std::string path;// normalized + decoded path (starts with '/')
	std::string query;// part after '?', without '?'
	std::string version;
	bool hadTrailingSlash = false;

	// store headers with LOWERCASE keys (case-insensitive behaviour)
	std::map<std::string, std::string> headers;

	std::string body;
};
