#pragma once

#include <string>
#include <vector>
#include <map>

struct LocationConfig
{
	std::string prefix;          // "/upload", "/images", "/"
	std::string root;            // "" или "static" и т.п. (относительно server.root)
	std::string index;           // "index.html"
	bool autoindex;              // on/off

	bool allowGet;
	bool allowHead;
	bool allowPost;
	bool allowDelete;

	LocationConfig()
		: prefix("/")
		, root("")
		, index("index.html")
		, autoindex(false)
		, allowGet(true)
		, allowHead(true)
		, allowPost(false)
		, allowDelete(false)
	{
	}
};

struct ServerConfig
{
	unsigned short listenPort;
	std::string root;                 // "www"
	std::string index;                // "index.html"
	std::string uploadDir;            // "www/uploads"
	std::size_t clientMaxBodySize;    // лимит body

	std::map<int, std::string> errorPages;  // 404 -> "errors/404.html"
	std::vector<LocationConfig> locations;

	ServerConfig()
		: listenPort(8080)
		, root("www")
		, index("index.html")
		, uploadDir("www/uploads")
		, clientMaxBodySize(1000000)
		, errorPages()
		, locations()
	{
		// дефолтный location "/"
		LocationConfig loc;
		loc.prefix = "/";
		loc.root = "";
		loc.index = index;
		loc.autoindex = false;

		loc.allowGet = true;
		loc.allowHead = true;
		loc.allowPost = true;
		loc.allowDelete = true;

		locations.push_back(loc);
	}
};
