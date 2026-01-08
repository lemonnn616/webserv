#include "ConfigParser.hpp"

static std::string joinArgs(const std::vector<std::string>& args)
{
	std::string s;
	for(std::size_t i=0;i<args.size();++i)
	{
		if(i)
			s+=" ";
		s+=args[i];
	}
	return s;
}

bool ConfigParser::parseTokens(const std::vector<Token>& t,std::vector<ServerConfig>& out)
{
	std::size_t i=0;

	while(i<t.size())
	{
		if(t[i].text=="server")
		{
			ServerConfig srv;
			++i;
			if(!parseServer(t,i,srv))
			{
				return false;
			}
			out.push_back(srv);
			continue;
		}

		return setError(t[i].line,"Unexpected token '"+t[i].text+"'");
	}

	return true;
}

bool ConfigParser::parseServer(const std::vector<Token>& t,std::size_t& i,ServerConfig& out)
{
	if(i>=t.size())
	{
		return setError(0,"Unexpected end of file after server");
	}

	if(t[i].text!="{")
	{
		return setError(t[i].line,"Expected '{' after server");
	}
	++i;

	while(i<t.size()&& t[i].text!="}")
	{
		if(t[i].text=="location")
		{
			if(!parseLocation(t,i,out))
			{
				return false;
			}
			continue;
		}

		if(!parseDirective(t,i,out,0))
		{
			return false;
		}
	}

	if(i>=t.size())
	{
		return setError(0,"Unexpected end of file in server block");
	}

	if(t[i].text!="}")
	{
		return setError(t[i].line,"Expected '}' to close server block");
	}
	++i;
	return true;
}

bool ConfigParser::parseLocation(const std::vector<Token>& t,std::size_t& i,ServerConfig& srv)
{
	if(i>=t.size()||t[i].text!="location")
	{
		return setError(0,"Internal parse error");
	}
	++i;

	if(i>=t.size())
	{
		return setError(0,"Unexpected end of file after location");
	}

	std::string prefix=t[i].text;
	std::size_t prefixLine=t[i].line;
	++i;

	if(i>=t.size())
	{
		return setError(0,"Unexpected end of file after location prefix");
	}

	if(t[i].text!="{")
	{
		return setError(prefixLine,"Expected '{' after location "+prefix);
	}
	++i;

	LocationConfig* loc=getOrCreateLocation(srv,prefix);
	if(loc==0)
	{
		return setError(prefixLine,"Failed to create location "+prefix);
	}

	while(i<t.size()&& t[i].text!="}")
	{
		if(!parseDirective(t,i,srv,loc))
		{
			return false;
		}
	}

	if(i>=t.size())
	{
		return setError(prefixLine,"Unexpected end of file in location "+prefix);
	}

	if(t[i].text!="}")
	{
		return setError(t[i].line,"Expected '}' to close location "+prefix);
	}
	++i;
	return true;
}

bool ConfigParser::parseDirective(const std::vector<Token>& t,std::size_t& i,ServerConfig& srv,LocationConfig* loc)
{
	if(i>=t.size())
	{
		return setError(0,"Unexpected end of file");
	}

	std::string key=t[i].text;
	std::size_t keyLine=t[i].line;
	++i;

	std::vector<std::string> args;
	while(i<t.size()&& t[i].text!=";"&& t[i].text!="{"&& t[i].text!="}")
	{
		args.push_back(t[i].text);
		++i;
	}

	if(i>=t.size())
	{
		return setError(keyLine,"Unexpected end of file after directive "+key);
	}

	if(t[i].text!=";")
	{
		return setError(t[i].line,"Expected ';' after directive "+key);
	}
	++i;

	bool ok=false;

	if(loc!=0)
	{
		ok=applyLocationDirective(srv,*loc,key,args);
	}
	else
	{
		ok=applyServerDirective(srv,key,args);
	}

	if(!ok)
	{
		return setError(keyLine,"Invalid directive '"+key+"' args '"+joinArgs(args)+"'");
	}

	return true;
}
