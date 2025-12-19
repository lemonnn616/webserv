#include "http/HttpRouter.hpp"
#include "utils/FileUtils.hpp"
#include "http/AutoIndex.hpp"
#include "http/ErrorPage.hpp"

static std::string getContentType(const std::string& p)
{
	std::size_t dot = p.rfind('.');
	if (dot == std::string::npos)
		return "application/octet-stream";

	std::string ext = p.substr(dot + 1);
	if (ext == "html") return "text/html";
	if (ext == "css")  return "text/css";
	if (ext == "js")   return "application/javascript";
	return "application/octet-stream";
}

HttpResponse HttpRouter::route(const HttpRequest& req, const ServerConfig& cfg)
{
	HttpResponse res;

	if (req.method != "GET" && req.method != "HEAD")
	{
		res.status = 405;
		res.reason = "Method Not Allowed";
		res.body = "Method Not Allowed\n";
		res.headers["Content-Length"] = "19";
		res.headers["Content-Type"] = "text/plain";
		return res;
	}

	std::string rel = (req.path == "/") ? cfg.index : req.path.substr(1);
	std::string fsPath = FileUtils::join(cfg.root, rel);

	if (FileUtils::isDirectory(fsPath))
	{
		std::string idx = FileUtils::join(fsPath, cfg.index);
		if (FileUtils::exists(idx))
			fsPath = idx;
		else
		{
			res.status = 403;
			res.reason = "Forbidden";
			res.body = ErrorPage::defaultHtml(403, "Forbidden");
			res.headers["Content-Type"] = "text/html";
			res.headers["Content-Length"] = std::to_string(res.body.size());
			return res;
		}
	}

	std::string body;
	if (!FileUtils::readFile(fsPath, body))
	{
		res.status = 404;
		res.reason = "Not Found";
		res.body = ErrorPage::defaultHtml(404, "Not Found");
		res.headers["Content-Type"] = "text/html";
		res.headers["Content-Length"] = std::to_string(res.body.size());
		return res;
	}

	res.status = 200;
	res.reason = "OK";
	res.headers["Content-Type"] = getContentType(fsPath);
	res.headers["Content-Length"] = std::to_string(body.size());

	if (req.method == "GET")
		res.body = body;
	else
		res.body = "";

	return res;
}
