#pragma once

#include <string>
#include <vector>
#include "ServerConfig.hpp"

class ConfigParser
{
public:
	ConfigParser();
	bool parseFile(const std::string& path,std::vector<ServerConfig>& out);

private:
	static std::string readFile(const std::string& path);
	static std::string stripComments(const std::string& s);
	static std::vector<std::string> tokenize(const std::string& s);

	bool parseTokens(const std::vector<std::string>& t,std::vector<ServerConfig>& out);
	bool parseServer(const std::vector<std::string>& t,std::size_t& i,ServerConfig& out);
	bool parseLocation(const std::vector<std::string>& t,std::size_t& i,ServerConfig& srv);
	bool parseDirective(const std::vector<std::string>& t,std::size_t& i,ServerConfig& srv,LocationConfig* loc);

	static bool applyServerDirective(ServerConfig& srv,const std::string& key,const std::vector<std::string>& args);
	static bool applyLocationDirective(ServerConfig& srv,LocationConfig& loc,const std::string& key,const std::vector<std::string>& args);

	static unsigned short parsePort(const std::string& s);
	static bool isNumber(const std::string& s);

	static void setAllowed(LocationConfig& loc,const std::vector<std::string>& args);
	static LocationConfig* getOrCreateLocation(ServerConfig& srv,const std::string& prefix);
};
