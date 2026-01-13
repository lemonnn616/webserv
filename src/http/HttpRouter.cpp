#include "http/HttpRouter.hpp"
#include "utils/FileUtils.hpp"
#include "http/AutoIndex.hpp"
#include "http/ErrorPage.hpp"

#include <ctime>
#include <cstdio>

// ---------- helpers ----------

static std::string getContentType(const std::string& p)
{
	std::size_t dot = p.rfind('.');
	if (dot == std::string::npos)
		return "application/octet-stream";

	std::string ext = p.substr(dot + 1);
	if (ext == "html") return "text/html";
	if (ext == "css")  return "text/css";
	if (ext == "js")   return "application/javascript";
	if (ext == "png")  return "image/png";
	if (ext == "jpg")  return "image/jpeg";
	if (ext == "jpeg") return "image/jpeg";
	if (ext == "gif")  return "image/gif";
	return "application/octet-stream";
}

static std::string makeUploadFileName()
{
	std::time_t t = std::time(0);
	return "upload_" + std::to_string((long long)t) + ".bin";
}

static bool containsDotDot(const std::string& s)
{
	return (s.find("..") != std::string::npos);
}

static const LocationConfig* matchLocation(const ServerConfig& cfg, const std::string& path)
{
	// locations уже отсортированы по длине prefix (normalizeAll)
	for (std::size_t i = 0; i < cfg.locations.size(); ++i)
	{
		const LocationConfig& loc = cfg.locations[i];
		const std::string& pre = loc.prefix;

		if (pre == "/")
			return &loc;

		// path starts with prefix?
		if (path.size() >= pre.size() && path.compare(0, pre.size(), pre) == 0)
		{
			// exact match
			if (path.size() == pre.size())
				return &loc;

			// if prefix ends with '/', ok
			if (!pre.empty() && pre[pre.size() - 1] == '/')
				return &loc;

			// boundary: next char must be '/'
			if (path[pre.size()] == '/')
				return &loc;
		}
	}
	return 0;
}

static void fillErrorHtml(HttpResponse& res, int code, const std::string& reason)
{
	res.status = code;
	res.reason = reason;
	res.body = ErrorPage::defaultHtml(code, reason);
	res.headers["Content-Type"] = "text/html";
	res.headers["Content-Length"] = std::to_string(res.body.size());
}

// ---------- main router ----------

HttpResponse HttpRouter::route(const HttpRequest& req, const ServerConfig& cfg)
{
	HttpResponse res;

	const LocationConfig* loc = matchLocation(cfg, req.path);
	if (!loc)
	{
		fillErrorHtml(res, 500, "Internal Server Error");
		return res;
	}

	// ----- method allowed? -----
	bool methodAllowed = false;

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

	// ----- redirect/return -----
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

	// ----- build filesystem path using location -----
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

	// ----- POST: upload body to uploadDir -----
	if (req.method == "POST")
	{
		std::string dir = cfg.uploadDir;
		if (dir.empty())
			dir = "www/uploads";

		std::string name = makeUploadFileName();
		std::string full = FileUtils::join(dir, name);

		if (!FileUtils::writeFile(full, req.body))
		{
			fillErrorHtml(res, 500, "Internal Server Error");
			return res;
		}

		res.status = 201;
		res.reason = "Created";
		res.body = "Uploaded: " + name + "\n";
		res.headers["Content-Type"] = "text/plain";
		res.headers["Content-Length"] = std::to_string(res.body.size());
		return res;
	}

	// ----- DELETE: delete file by fsPath -----
	if (req.method == "DELETE")
	{
		if (containsDotDot(req.path))
		{
			res.status = 400;
			res.reason = "Bad Request";
			res.body = "Bad path\n";
			res.headers["Content-Type"] = "text/plain";
			res.headers["Content-Length"] = std::to_string(res.body.size());
			return res;
		}

		if (FileUtils::isDirectory(fsPath))
		{
			res.status = 403;
			res.reason = "Forbidden";
			res.body = "Cannot delete directory\n";
			res.headers["Content-Type"] = "text/plain";
			res.headers["Content-Length"] = std::to_string(res.body.size());
			return res;
		}

		if (::remove(fsPath.c_str()) != 0)
		{
			res.status = 404;
			res.reason = "Not Found";
			res.body = "Not Found\n";
			res.headers["Content-Type"] = "text/plain";
			res.headers["Content-Length"] = std::to_string(res.body.size());
			return res;
		}

		res.status = 204;
		res.reason = "No Content";
		res.body = "";
		res.headers["Content-Length"] = "0";
		return res;
	}

	// ----- directory handling: index / autoindex -----
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

			fillErrorHtml(res, 403, "Forbidden");
			return res;
		}
	}

	// ----- serve file (GET/HEAD) -----
	std::string body;
	if (!FileUtils::readFile(fsPath, body))
	{
		fillErrorHtml(res, 404, "Not Found");
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
