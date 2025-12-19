#pragma once

#include <string>

struct ServerConfig;

class ConfigParser
{
public:
	ConfigParser();
	bool parseFile(const std::string& path,ServerConfig& out);

private:
	static std::string trim(const std::string& s);
	static std::string stripComment(const std::string& s);
};
