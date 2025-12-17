#pragma once

#include <string>
#include "config/ServerConfig.hpp"

class ConfigParser
{
public:
	ConfigParser();
	bool parseFile(const std::string& path, ServerConfig& out);

private:
	static std::string trim(const std::string& s);
	static std::string stripComment(const std::string& s);
};
