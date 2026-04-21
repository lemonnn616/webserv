#pragma once
#include <string>
#include <map>

struct HttpResponse
{
	std::string version; 
	int status;
	std::string reason;
	std::map<std::string, std::string> headers;
	std::string body;

	HttpResponse()
		: version("HTTP/1.1"), status(200), reason("OK"), headers(), body()
	{
	}

	std::string serialize() const;
};
