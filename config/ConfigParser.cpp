#include "ConfigParser.hpp"
#include "core/Logger.hpp"

#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdlib>

ConfigParser::ConfigParser()
{
}

std::string ConfigParser::readFile(const std::string& path)
{
	std::ifstream f(path.c_str(),std::ios::in|std::ios::binary);
	if(!f)
	{
		return "";
	}

	std::ostringstream ss;
	ss<<f.rdbuf();
	return ss.str();
}

std::string ConfigParser::stripComments(const std::string& s)
{
	std::string out;
	out.reserve(s.size());

	std::size_t i=0;
	while(i<s.size())
	{
		if(s[i]=='#')
		{
			while(i<s.size()&& s[i]!='\n')
			{
				++i;
			}
			continue;
		}
		out.push_back(s[i]);
		++i;
	}
	return out;
}

std::vector<std::string> ConfigParser::tokenize(const std::string& s)
{
	std::vector<std::string> t;
	std::string cur;

	for(std::size_t i=0;i<s.size();++i)
	{
		char c=s[i];

		if(c=='{'||c=='}'||c==';')
		{
			if(!cur.empty())
			{
				t.push_back(cur);
				cur.clear();
			}
			std::string one;
			one.push_back(c);
			t.push_back(one);
			continue;
		}

		if(std::isspace(static_cast<unsigned char>(c)))
		{
			if(!cur.empty())
			{
				t.push_back(cur);
				cur.clear();
			}
			continue;
		}

		cur.push_back(c);
	}

	if(!cur.empty())
	{
		t.push_back(cur);
	}
	return t;
}

bool ConfigParser::parseFile(const std::string& path,std::vector<ServerConfig>& out)
{
	out.clear();

	std::string content=readFile(path);
	if(content.empty())
	{
		Logger::warn("Config not found, using defaults: "+path);
		out.push_back(ServerConfig());
		return true;
	}

	content=stripComments(content);
	std::vector<std::string> t=tokenize(content);

	if(!parseTokens(t,out))
	{
		return false;
	}

	if(out.empty())
	{
		out.push_back(ServerConfig());
	}
	return true;
}

bool ConfigParser::parseTokens(const std::vector<std::string>& t,std::vector<ServerConfig>& out)
{
	std::size_t i=0;

	while(i<t.size())
	{
		if(t[i]=="server")
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
		++i;
	}
	return true;
}

bool ConfigParser::parseServer(const std::vector<std::string>& t,std::size_t& i,ServerConfig& out)
{
	if(i>=t.size()||t[i]!="{")
	{
		return false;
	}
	++i;

	while(i<t.size()&& t[i]!="}")
	{
		if(t[i]=="location")
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

	if(i>=t.size()||t[i]!="}")
	{
		return false;
	}
	++i;
	return true;
}

bool ConfigParser::parseLocation(const std::vector<std::string>& t,std::size_t& i,ServerConfig& srv)
{
	if(i>=t.size()||t[i]!="location")
	{
		return false;
	}
	++i;

	if(i>=t.size())
	{
		return false;
	}
	std::string prefix=t[i];
	++i;

	if(i>=t.size()||t[i]!="{")
	{
		return false;
	}
	++i;

	LocationConfig* loc=getOrCreateLocation(srv,prefix);
	if(loc==0)
	{
		return false;
	}

	while(i<t.size()&& t[i]!="}")
	{
		if(!parseDirective(t,i,srv,loc))
		{
			return false;
		}
	}

	if(i>=t.size()||t[i]!="}")
	{
		return false;
	}
	++i;
	return true;
}

bool ConfigParser::parseDirective(const std::vector<std::string>& t,std::size_t& i,ServerConfig& srv,LocationConfig* loc)
{
	if(i>=t.size())
	{
		return false;
	}

	std::string key=t[i];
	++i;

	std::vector<std::string> args;
	while(i<t.size()&& t[i]!=";"&& t[i]!="{"&& t[i]!="}")
	{
		args.push_back(t[i]);
		++i;
	}

	if(i>=t.size()||t[i]!=";")
	{
		return false;
	}
	++i;

	if(loc!=0)
	{
		return applyLocationDirective(srv,*loc,key,args);
	}
	return applyServerDirective(srv,key,args);
}

bool ConfigParser::applyServerDirective(ServerConfig& srv,const std::string& key,const std::vector<std::string>& args)
{
	if(key=="listen")
	{
		if(args.size()<1)
		{
			return false;
		}
		srv.listenPort=parsePort(args[0]);
		return true;
	}

	if(key=="server_name")
	{
		for(std::size_t i=0;i<args.size();++i)
		{
			srv.serverNames.push_back(args[i]);
		}
		return true;
	}

	if(key=="root")
	{
		if(args.size()<1)
		{
			return false;
		}
		srv.root=args[0];
		return true;
	}

	if(key=="index")
	{
		if(args.size()<1)
		{
			return false;
		}
		srv.index=args[0];
		return true;
	}

	if(key=="upload_dir")
	{
		if(args.size()<1)
		{
			return false;
		}
		srv.uploadDir=args[0];
		return true;
	}

	if(key=="client_max_body_size")
	{
		if(args.size()<1)
		{
			return false;
		}
		long n=std::atol(args[0].c_str());
		if(n>0)
		{
			srv.clientMaxBodySize=static_cast<std::size_t>(n);
		}
		return true;
	}

	if(key=="error_page")
	{
		if(args.size()<2)
		{
			return false;
		}

		std::string path=args[args.size()-1];

		for(std::size_t i=0;i+1<args.size();++i)
		{
			if(isNumber(args[i]))
			{
				int code=std::atoi(args[i].c_str());
				if(code>0)
				{
					srv.errorPages[code]=path;
				}
			}
		}
		return true;
	}

	if(key=="cgi")
	{
		if(args.size()<2)
		{
			return false;
		}
		srv.cgi[args[0]]=args[1];
		return true;
	}

	if(key=="session")
	{
		if(args.size()<1)
		{
			return false;
		}
		srv.sessionEnabled=(args[0]=="on");
		return true;
	}

	if(key=="session_timeout")
	{
		if(args.size()<1)
		{
			return false;
		}
		long n=std::atol(args[0].c_str());
		if(n>0)
		{
			srv.sessionTimeout=static_cast<std::size_t>(n);
		}
		return true;
	}

	if(key=="session_store_path")
	{
		if(args.size()<1)
		{
			return false;
		}
		srv.sessionStorePath=args[0];
		return true;
	}

	return true;
}

bool ConfigParser::applyLocationDirective(ServerConfig& srv,LocationConfig& loc,const std::string& key,const std::vector<std::string>& args)
{
	(void)srv;

	if(key=="root")
	{
		if(args.size()<1)
		{
			return false;
		}
		loc.root=args[0];
		return true;
	}

	if(key=="index")
	{
		if(args.size()<1)
		{
			return false;
		}
		loc.index=args[0];
		return true;
	}

	if(key=="autoindex")
	{
		if(args.size()<1)
		{
			return false;
		}
		loc.autoindex=(args[0]=="on");
		return true;
	}

	if(key=="allowed_methods")
	{
		setAllowed(loc,args);
		return true;
	}

	if(key=="return"||key=="redirect")
	{
		if(args.size()>=2&& isNumber(args[0]))
		{
			loc.hasReturn=true;
			loc.returnCode=std::atoi(args[0].c_str());
			loc.returnUrl=args[1];
			return true;
		}
		if(args.size()>=1)
		{
			loc.hasReturn=true;
			loc.returnCode=302;
			loc.returnUrl=args[0];
			return true;
		}
		return false;
	}

	return true;
}

unsigned short ConfigParser::parsePort(const std::string& s)
{
	std::string p=s;
	std::size_t pos=p.rfind(':');
	if(pos!=std::string::npos)
	{
		p=p.substr(pos+1);
	}

	int n=std::atoi(p.c_str());
	if(n<1||n>65535)
	{
		return 8080;
	}
	return static_cast<unsigned short>(n);
}

bool ConfigParser::isNumber(const std::string& s)
{
	if(s.empty())
	{
		return false;
	}

	for(std::size_t i=0;i<s.size();++i)
	{
		if(!std::isdigit(static_cast<unsigned char>(s[i])))
		{
			return false;
		}
	}
	return true;
}

void ConfigParser::setAllowed(LocationConfig& loc,const std::vector<std::string>& args)
{
	loc.allowGet=false;
	loc.allowHead=false;
	loc.allowPost=false;
	loc.allowDelete=false;

	if(args.empty())
	{
		return;
	}

	for(std::size_t i=0;i<args.size();++i)
	{
		const std::string& m=args[i];

		if(m=="ALL")
		{
			loc.allowGet=true;
			loc.allowHead=true;
			loc.allowPost=true;
			loc.allowDelete=true;
			continue;
		}
		if(m=="GET")
		{
			loc.allowGet=true;
			continue;
		}
		if(m=="HEAD")
		{
			loc.allowHead=true;
			continue;
		}
		if(m=="POST")
		{
			loc.allowPost=true;
			continue;
		}
		if(m=="DELETE")
		{
			loc.allowDelete=true;
			continue;
		}
	}
}

LocationConfig* ConfigParser::getOrCreateLocation(ServerConfig& srv,const std::string& prefix)
{
	for(std::size_t i=0;i<srv.locations.size();++i)
	{
		if(srv.locations[i].prefix==prefix)
		{
			return &srv.locations[i];
		}
	}

	LocationConfig loc;
	loc.prefix=prefix;
	srv.locations.push_back(loc);
	return &srv.locations[srv.locations.size()-1];
}
