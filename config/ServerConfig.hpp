#pragma once

#include <string>

struct ServerConfig
{
	unsigned short listenPort;
	std::string root;
	std::string index;
	std::string uploadDir;
	std::size_t clientMaxBodySize;

	ServerConfig()
		: listenPort(8080)
		, root("www")
		, index("index.html")
		, uploadDir("www/uploads")
		, clientMaxBodySize(1000000)
	{
	}
};
