#pragma once
#include <string>
#include <map>

struct HttpResponse
{
	int status;
	std::string reason;
	std::map<std::string, std::string> headers;
	std::string body;

	std::string serialize() const;
};
