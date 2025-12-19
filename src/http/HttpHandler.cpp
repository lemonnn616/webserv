#include "http/HttpHandler.hpp"
#include "http/HttpParser.hpp"
#include "http/HttpRouter.hpp"


HttpHandler::~HttpHandler() {}

HttpHandler::HttpHandler()
	: _cfgs(0)
{
}

void HttpHandler::setServerConfigs(const std::vector<ServerConfig>* cfgs)
{
	_cfgs = cfgs;
}

void HttpHandler::onDataReceived(
	int,
	std::string& inBuffer,
	std::string& outBuffer,
	ConnectionState& state,
	std::size_t serverConfigIndex,
	std::string&
)
{
	if (!_cfgs || _cfgs->empty())
		return;

	const ServerConfig& cfg =
		(serverConfigIndex < _cfgs->size()) ? (*_cfgs)[serverConfigIndex] : (*_cfgs)[0];

	HttpRequest req;
	if (!HttpParser::parse(inBuffer, req, cfg.clientMaxBodySize))
		return;

	HttpResponse res = HttpRouter::route(req, cfg);
	outBuffer = res.serialize();
	state = ConnectionState::WRITING;
}
