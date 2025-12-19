#pragma once

#include <string>
#include <vector>
#include <map>

struct LocationConfig
{
	std::string prefix;
	std::string root;
	std::string index;
	bool autoindex;

	bool allowGet;
	bool allowHead;
	bool allowPost;
	bool allowDelete;

	bool hasReturn;
	int returnCode;
	std::string returnUrl;

	LocationConfig()
		: prefix("/")
		, root("")
		, index("index.html")
		, autoindex(false)
		, allowGet(true)
		, allowHead(true)
		, allowPost(false)
		, allowDelete(false)
		, hasReturn(false)
		, returnCode(0)
		, returnUrl("")
	{
	}
};

struct ServerConfig
{
	unsigned short listenPort;
	std::vector<std::string> serverNames;

	std::string root;
	std::string index;
	std::string uploadDir;
	std::size_t clientMaxBodySize;

	std::map<int, std::string> errorPages;
	std::map<std::string, std::string> cgi;

	bool sessionEnabled;
	std::size_t sessionTimeout;
	std::string sessionStorePath;

	std::vector<LocationConfig> locations;

	ServerConfig()
		: listenPort(8080)
		, serverNames()
		, root("www")
		, index("index.html")
		, uploadDir("www/uploads")
		, clientMaxBodySize(1000000)
		, errorPages()
		, cgi()
		, sessionEnabled(false)
		, sessionTimeout(0)
		, sessionStorePath("")
		, locations()
	{
		LocationConfig loc;
		loc.prefix="/";
		loc.root="";
		loc.index=index;
		loc.autoindex=false;

		loc.allowGet=true;
		loc.allowHead=true;
		loc.allowPost=true;
		loc.allowDelete=true;

		locations.push_back(loc);
	}
};
