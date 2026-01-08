#include "ConfigParser.hpp"
#include "core/Logger.hpp"

ConfigParser::ConfigParser()
	: _error()
{
}

const std::string& ConfigParser::getError() const
{
	return _error;
}

void ConfigParser::clearError()
{
	_error.clear();
}

bool ConfigParser::setError(std::size_t line,const std::string& msg)
{
	if(!_error.empty())
	{
		return false;
	}

	if(line>0)
	{
		_error="line "+std::to_string(line)+": "+msg;
	}
	else
	{
		_error=msg;
	}
	return false;
}

bool ConfigParser::parseFile(const std::string& path,std::vector<ServerConfig>& out)
{
	clearError();
	out.clear();

	std::string content=readFile(path);
	if(content.empty())
	{
		Logger::warn("Config not found, using defaults: "+path);
		out.push_back(ServerConfig());
		return true;
	}

	content=stripComments(content);
	std::vector<Token> t=tokenize(content);

	if(!parseTokens(t,out))
	{
		if(_error.empty())
			setError(0,"Bad config");
		return false;
	}

	if(out.empty())
	{
		out.push_back(ServerConfig());
	}

	if(!normalizeAll(out))
	{
		if(_error.empty())
			setError(0,"Bad config");
		return false;
	}

	return true;
}
