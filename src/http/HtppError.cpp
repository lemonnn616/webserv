#include "http/HttpError.hpp"
#include "http/ErrorPage.hpp"
#include "utils/FileUtils.hpp"

static std::string getContentTypeByPath(const std::string& p)
{
	std::size_t dot = p.rfind('.');
	if (dot == std::string::npos)
		return "application/octet-stream";

	std::string ext = p.substr(dot + 1);
	if (ext == "html")	return "text/html";
	if (ext == "css")	return "text/css";
	if (ext == "js")	return "application/javascript";
	return "application/octet-stream";
}

void HttpError::fill(HttpResponse& res, const ServerConfig& cfg, int code, const std::string& reason)
{
	res.status = code;
	res.reason = reason;

	std::map<int, std::string>::const_iterator it = cfg.errorPages.find(code);
	if (it != cfg.errorPages.end())
	{
		std::string body;
		if (FileUtils::readFile(it->second, body))
		{
			res.body = body;
			res.headers["Content-Type"] = getContentTypeByPath(it->second);
			res.headers["Content-Length"] = std::to_string(res.body.size());
			return;
		}
	}

	res.body = ErrorPage::defaultHtml(code, reason);
	res.headers["Content-Type"] = "text/html";
	res.headers["Content-Length"] = std::to_string(res.body.size());
}
