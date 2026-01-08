#include "ConfigParser.hpp"

#include <cstdlib>
#include <cctype>

bool ConfigParser::applyServerDirective(ServerConfig& srv,const std::string& key,const std::vector<std::string>& args)
{
	if(key=="listen")
	{
		if(args.size()!=1)
			return false;

		unsigned short p=parsePort(args[0]);
		if(p==0)
			return false;

		srv.listenPort=p;
		return true;
	}

	if(key=="server_name")
	{
		if(args.empty())
			return false;

		for(std::size_t i=0;i<args.size();++i)
		{
			srv.serverNames.push_back(args[i]);
		}
		return true;
	}

	if(key=="root")
	{
		if(args.size()!=1)
			return false;

		srv.root=args[0];
		return true;
	}

	if(key=="index")
	{
		if(args.size()!=1)
			return false;

		srv.index=args[0];
		return true;
	}

	if(key=="upload_dir")
	{
		if(args.size()!=1)
			return false;

		srv.uploadDir=args[0];
		return true;
	}

	if(key=="client_max_body_size")
	{
		if(args.size()!=1)
			return false;

		long n=std::atol(args[0].c_str());
		if(n<=0)
			return false;

		srv.clientMaxBodySize=static_cast<std::size_t>(n);
		return true;
	}

	if(key=="error_page")
	{
		if(args.size()<2)
			return false;

		std::string path=args[args.size()-1];
		bool any=false;

		for(std::size_t i=0;i+1<args.size();++i)
		{
			if(isNumber(args[i]))
			{
				int code=std::atoi(args[i].c_str());
				if(code>0)
				{
					srv.errorPages[code]=path;
					any=true;
				}
			}
			else
			{
				return false;
			}
		}

		return any;
	}

	if(key=="cgi")
	{
		if(args.size()!=2)
			return false;

		srv.cgi[args[0]]=args[1];
		return true;
	}

	if(key=="session")
	{
		if(args.size()!=1)
			return false;

		if(args[0]!="on"&& args[0]!="off")
			return false;

		srv.sessionEnabled=(args[0]=="on");
		return true;
	}

	if(key=="session_timeout")
	{
		if(args.size()!=1)
			return false;

		long n=std::atol(args[0].c_str());
		if(n<=0)
			return false;

		srv.sessionTimeout=static_cast<std::size_t>(n);
		return true;
	}

	if(key=="session_store_path")
	{
		if(args.size()!=1)
			return false;

		srv.sessionStorePath=args[0];
		return true;
	}

	return false;
}

bool ConfigParser::applyLocationDirective(ServerConfig& srv,LocationConfig& loc,const std::string& key,const std::vector<std::string>& args)
{
	(void)srv;

	if(key=="root")
	{
		if(args.size()!=1)
			return false;

		loc.root=args[0];
		return true;
	}

	if(key=="index")
	{
		if(args.size()!=1)
			return false;

		loc.index=args[0];
		return true;
	}

	if(key=="autoindex")
	{
		if(args.size()!=1)
			return false;

		if(args[0]!="on"&& args[0]!="off")
			return false;

		loc.autoindex=(args[0]=="on");
		return true;
	}

	if(key=="allowed_methods")
	{
		if(args.empty())
			return false;

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
		if(args.size()==1)
		{
			loc.hasReturn=true;
			loc.returnCode=302;
			loc.returnUrl=args[0];
			return true;
		}
		return false;
	}

	return false;
}

unsigned short ConfigParser::parsePort(const std::string& s)
{
	std::string p=s;
	std::size_t pos=p.rfind(':');
	if(pos!=std::string::npos)
	{
		p=p.substr(pos+1);
	}

	if(p.empty())
		return 0;

	for(std::size_t i=0;i<p.size();++i)
	{
		if(!std::isdigit(static_cast<unsigned char>(p[i])))
			return 0;
	}

	long n=std::atol(p.c_str());
	if(n<1||n>65535)
		return 0;

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
