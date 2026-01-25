#include "http/HttpHandler.hpp"
#include "http/HttpParser.hpp"
#include "http/HttpRouter.hpp"
#include "http/HttpError.hpp"

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

	const ServerConfig* cfg;
	if (serverConfigIndex < _cfgs->size())
		cfg = &(*_cfgs)[serverConfigIndex];
	else
		cfg = &(*_cfgs)[0];

	HttpRequest req;
	HttpParser::Result r = HttpParser::parse(inBuffer, req, cfg->clientMaxBodySize);

	if (r == HttpParser::NEED_MORE)
	{
		state = ConnectionState::READING;
		return;
	}

	HttpResponse res;

	if (r == HttpParser::BAD_REQUEST)
	{
		HttpError::fill(res, *cfg, 400, "Bad Request");
		res.headers["Connection"] = "close";
		// req может быть невалидным — не трогаем res.version (пусть будет HTTP/1.1 по умолчанию)
		outBuffer = res.serialize();
		state = ConnectionState::WRITING;
		return;
	}

	if (r == HttpParser::TOO_LARGE)
	{
		HttpError::fill(res, *cfg, 413, "Payload Too Large");
		res.headers["Connection"] = "close";
		// req может быть невалидным — не трогаем res.version
		outBuffer = res.serialize();
		state = ConnectionState::WRITING;
		return;
	}

	// OK: теперь req валиден => можно вернуть ту же версию
	res = HttpRouter::route(req, *cfg);
	
	// всегда закрываем соединение — упрощает Core и nc/curl
	res.headers["Connection"] = "close";

	// версия известна, т.к. parse == OK
	res.version = req.version;

	outBuffer = res.serialize();
	state = ConnectionState::WRITING;
}
