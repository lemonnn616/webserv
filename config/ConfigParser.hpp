#pragma once

#include <string>
#include <vector>
#include "ServerConfig.hpp"

class ConfigParser
{
public:
	ConfigParser();
	bool parseFile(const std::string& path,std::vector<ServerConfig>& out);
	const std::string& getError() const;

private:
	struct Token
	{
		std::string text;
		std::size_t line;
	};

	std::string _error;

	void clearError();
	bool setError(std::size_t line,const std::string& msg);

	static std::string readFile(const std::string& path);
	static std::string stripComments(const std::string& s);
	static std::vector<Token> tokenize(const std::string& s);

	bool parseTokens(const std::vector<Token>& t,std::vector<ServerConfig>& out);
	bool parseServer(const std::vector<Token>& t,std::size_t& i,ServerConfig& out);
	bool parseLocation(const std::vector<Token>& t,std::size_t& i,ServerConfig& srv);
	bool parseDirective(const std::vector<Token>& t,std::size_t& i,ServerConfig& srv,LocationConfig* loc);

	static bool applyServerDirective(ServerConfig& srv,const std::string& key,const std::vector<std::string>& args);
	static bool applyLocationDirective(ServerConfig& srv,LocationConfig& loc,const std::string& key,const std::vector<std::string>& args);

	static unsigned short parsePort(const std::string& s);
	static bool isNumber(const std::string& s);

	static void setAllowed(LocationConfig& loc,const std::vector<std::string>& args);
	static LocationConfig* getOrCreateLocation(ServerConfig& srv,const std::string& prefix);

	bool normalizeAll(std::vector<ServerConfig>& out);
};
