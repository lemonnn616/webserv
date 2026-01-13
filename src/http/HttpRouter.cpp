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

static const LocationConfig* matchLocation(const ServerConfig& cfg, const std::string& path)
{
	// locations уже отсортированы по длине prefix (у тебя normalizeAll это делает)
	for (std::size_t i = 0; i < cfg.locations.size(); ++i)
	{
		const LocationConfig& loc = cfg.locations[i];
		const std::string& pre = loc.prefix;

		if (pre == "/")
			return &loc;

		// проверка: path начинается с pre
		if (path.size() >= pre.size() && path.compare(0, pre.size(), pre) == 0)
		{
			// чтобы /upload не матчился на /uploads123 случайно:
			// если prefix не заканчивается '/', то следующий символ должен быть '/' или конец строки
			if (path.size() == pre.size())
				return &loc;

			if (pre[pre.size() - 1] == '/')
				return &loc;

			if (path[pre.size()] == '/')
				return &loc;
		}
	}
	// fallback: если почему-то locations пустые (не должно быть), возвращаем null
	return 0;
}


HttpResponse HttpRouter::route(const HttpRequest& req, const ServerConfig& cfg)
{
	HttpResponse res;
	bool methodAllowed = false;

	const LocationConfig* loc = matchLocation(cfg, req.path);
	if (!loc)
	{
		res.status = 500;
		res.reason = "Internal Server Error";
		res.body = ErrorPage::defaultHtml(500, "Internal Server Error");
		res.headers["Content-Type"] = "text/html";
		res.headers["Content-Length"] = std::to_string(res.body.size());
		return res;
	}

		if (req.method == "GET" && loc->allowGet)
			methodAllowed = true;
		else if (req.method == "HEAD" && loc->allowHead)
			methodAllowed = true;
		else if (req.method == "POST" && loc->allowPost)
			methodAllowed = true;
		else if (req.method == "DELETE" && loc->allowDelete)
			methodAllowed = true;

		if (!methodAllowed)
		{
			res.status = 405;
			res.reason = "Method Not Allowed";
			res.body = "Method Not Allowed\n";
			res.headers["Content-Type"] = "text/plain";
			res.headers["Content-Length"] = std::to_string(res.body.size());
			return res;
		}
		if (loc->hasReturn)
		{
			int code = loc->returnCode;
			if (code <= 0)
				code = 302;

			res.status = code;
			res.reason = "Found";
			res.headers["Location"] = loc->returnUrl;
			res.body = "";
			res.headers["Content-Length"] = "0";
			return res;
		}

			std::string baseRoot = cfg.root;
		if (!loc->root.empty())
			baseRoot = loc->root;

			std::string indexName = cfg.index;
		if (!loc->index.empty())
			indexName = loc->index;

		std::string relPath;

		if (loc->prefix == "/")
		{
			if (req.path.size() > 1)
				relPath = req.path.substr(1);
			else
				relPath = "";
		}
		else
		{
			if (req.path.size() > loc->prefix.size())
			{
				relPath = req.path.substr(loc->prefix.size());
				if (!relPath.empty() && relPath[0] == '/')
					relPath.erase(0, 1);
			}
			else				
			{
				relPath = "";
			}
		}

std::string fsPath = FileUtils::join(baseRoot, relPath);

	if (FileUtils::isDirectory(fsPath))
	{
		std::string idx = FileUtils::join(fsPath, indexName);

		if (FileUtils::exists(idx))
		{
			fsPath = idx;
		}
		else
		{
			if (loc->autoindex)
			{
				std::string html = AutoIndex::generate(req.path, fsPath);

				res.status = 200;
				res.reason = "OK";
				res.body = html;
				res.headers["Content-Type"] = "text/html";
				res.headers["Content-Length"] = std::to_string(res.body.size());

				if (req.method == "HEAD")
					res.body = "";

				return res;
			}

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
