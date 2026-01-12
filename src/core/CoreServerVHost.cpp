#include "core/CoreServer.hpp"

#include <cctype>

static std::string toLowerStr(const std::string& s)
{
	std::string r=s;
	for(std::size_t i=0;i<r.size();++i)
	{
		r[i]=static_cast<char>(std::tolower(static_cast<unsigned char>(r[i])));
	}
	return r;
}

static std::string ltrimSpaces(const std::string& s)
{
	std::size_t i=0;
	while(i<s.size()&&(s[i]==' '||s[i]=='\t'))
	{
		++i;
	}
	return s.substr(i);
}

static std::string extractHostFromHeadersBlock(const std::string& headersBlock)
{
	std::size_t lineEnd=headersBlock.find("\r\n");
	if(lineEnd==std::string::npos)
	{
		return "";
	}

	std::size_t pos=lineEnd+2;

	while(pos<headersBlock.size())
	{
		std::size_t end=headersBlock.find("\r\n",pos);
		if(end==std::string::npos)
		{
			break;
		}

		std::string line=headersBlock.substr(pos,end-pos);
		pos=end+2;

		if(line.empty())
		{
			break;
		}

		std::size_t colon=line.find(':');
		if(colon==std::string::npos)
		{
			continue;
		}

		std::string key=toLowerStr(line.substr(0,colon));
		std::string val=ltrimSpaces(line.substr(colon+1));

		if(key=="host")
		{
			if(!val.empty()&& val[0]=='[')
			{
				std::size_t close=val.find(']');
				if(close!=std::string::npos&& close>1)
				{
					val=val.substr(1,close-1);
				}
			}
			else
			{
				std::size_t p=val.find(':');
				if(p!=std::string::npos)
				{
					val=val.substr(0,p);
				}
			}
			return toLowerStr(val);
		}
	}

	return "";
}

std::size_t CoreServer::selectServerIndexByHost(unsigned short port,std::size_t defaultIndex,const std::string& host) const
{
	std::map<unsigned short,std::map<std::string,std::size_t> >::const_iterator itPort=_serverByPortHost.find(port);
	if(itPort!=_serverByPortHost.end())
	{
		std::map<std::string,std::size_t>::const_iterator itHost=itPort->second.find(host);
		if(itHost!=itPort->second.end())
		{
			return itHost->second;
		}
	}

	std::map<unsigned short,std::size_t>::const_iterator itDef=_defaultServerByPort.find(port);
	if(itDef!=_defaultServerByPort.end())
	{
		return itDef->second;
	}

	return defaultIndex;
}

void CoreServer::updateServerIndexFromHost(Client& client)
{
	std::size_t headersEnd=client.inBuffer.find("\r\n\r\n");
	if(headersEnd==std::string::npos)
	{
		return;
	}

	std::string headersBlock=client.inBuffer.substr(0,headersEnd);
	std::string host=extractHostFromHeadersBlock(headersBlock);
	if(host.empty())
	{
		return;
	}

	std::size_t def=client.serverConfigIndex;
	if(def>=_serverConfigs.size())
	{
		def=0;
	}

	unsigned short port=client.listenPort;
	if(port==0&& def<_serverConfigs.size())
	{
		port=_serverConfigs[def].listenPort;
	}

	client.serverConfigIndex=selectServerIndexByHost(port,def,host);
}
