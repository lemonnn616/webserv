#include "http/HttpHandler.hpp"
#include "http/HttpParser.hpp"
#include "http/HttpRouter.hpp"
#include "http/HttpError.hpp"
#include "cgi/CgiRunner.hpp"

HttpHandler::~HttpHandler() {}

HttpHandler::HttpHandler()
	: _cfgs(0)
{
}

void HttpHandler::setServerConfigs(const std::vector<ServerConfig>* cfgs)
{
	_cfgs = cfgs;
}

static void fillBadGateway(HttpResponse& res,const ServerConfig& cfg,const HttpRequest& req)
{
	HttpError::fill(res,cfg,502,"Bad Gateway");
	res.headers["Connection"]="close";
	if(req.method=="HEAD")
		res.body="";
}

void HttpHandler::onDataReceived(
	int fd,
	std::string& inBuffer,
	std::string& outBuffer,
	ConnectionState& state,
	std::size_t serverConfigIndex,
	std::string& stateData
)
{
	(void)fd;

	if(!_cfgs||_cfgs->empty())
		return;

	const ServerConfig* cfg;
	if(serverConfigIndex<_cfgs->size())
		cfg=&(*_cfgs)[serverConfigIndex];
	else
		cfg=&(*_cfgs)[0];

	HttpRequest req;
	HttpParser::Result r=HttpParser::parse(inBuffer,req,cfg->clientMaxBodySize);

	if(r==HttpParser::NEED_MORE)
	{
		state=ConnectionState::READING;
		return;
	}

	HttpResponse res;

	if(r==HttpParser::BAD_REQUEST)
	{
		HttpError::fill(res,*cfg,400,"Bad Request");
		res.headers["Connection"]="close";
		outBuffer=res.serialize();
		state=ConnectionState::WRITING;
		return;
	}

	if(r==HttpParser::TOO_LARGE)
	{
		HttpError::fill(res,*cfg,413,"Payload Too Large");
		res.headers["Connection"]="close";
		outBuffer=res.serialize();
		state=ConnectionState::WRITING;
		return;
	}

	HttpRouter::RouteResult rr=HttpRouter::route2(req,*cfg);

	if(rr.isCgi)
	{
		CgiRunner::Spawned sp;
		std::map<std::string,std::string> extra;

		if(!CgiRunner::spawn(rr.cgiInterpreter,rr.cgiScriptPath,req,extra,sp))
		{
			fillBadGateway(res,*cfg,req);
			outBuffer=res.serialize();
			state=ConnectionState::WRITING;
			return;
		}

		// Формат stateData:
		// CGI|<pid>|<stdinFd>|<stdoutFd>|<stderrFd>|<method>|<version>|<len>\n<body>
		stateData.clear();
		stateData+="CGI|";
		stateData+=std::to_string((long long)sp.pid);
		stateData+="|";
		stateData+=std::to_string(sp.stdinFd);
		stateData+="|";
		stateData+=std::to_string(sp.stdoutFd);
		stateData+="|";
		stateData+=std::to_string(sp.stderrFd);
		stateData+="|";
		stateData+=req.method;
		stateData+="|";
		stateData+=req.version;
		stateData+="|";
		stateData+=std::to_string(req.body.size());
		stateData+="\n";
		stateData.append(req.body);

		state=ConnectionState::CGI_PENDING;
		return;
	}

	res=rr.response;
	res.headers["Connection"]="close";
	res.version=req.version;

	outBuffer=res.serialize();
	state=ConnectionState::WRITING;
}
