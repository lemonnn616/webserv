#include "http/HttpRouter.hpp"
#include "http/HttpError.hpp"
#include "utils/FileUtils.hpp"
#include "http/AutoIndex.hpp"

#include <ctime>
#include <cstdio>
#include <string>
#include <cctype>

// ---------- helpers ----------

static std::string toLowerStr(const std::string& s)
{
	std::string r = s;
	for (std::size_t i = 0; i < r.size(); ++i)
		r[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(r[i])));
	return r;
}

static void applyConnectionPolicy(const HttpRequest& req, HttpResponse& res)
{
	// headers in req are stored lowercase (by your parser), so "connection" is lowercase
	std::string raw = "";
	std::map<std::string, std::string>::const_iterator it = req.headers.find("connection");
	if (it != req.headers.end())
		raw = it->second;

	std::string c = toLowerStr(raw);

	if (req.version == "HTTP/1.0")
	{
		// HTTP/1.0 default: close
		if (c.find("keep-alive") != std::string::npos)
			res.headers["Connection"] = "keep-alive";
		else
			res.headers["Connection"] = "close";
	}
	else
	{
		// HTTP/1.1 default: keep-alive
		if (c.find("close") != std::string::npos)
			res.headers["Connection"] = "close";
		else
			res.headers["Connection"] = "keep-alive";
	}
}

static std::string buildAllowHeader(const LocationConfig& loc)
{
	std::string allow;

	if (loc.allowGet)    { if (!allow.empty()) allow += ", "; allow += "GET"; }
	if (loc.allowHead)   { if (!allow.empty()) allow += ", "; allow += "HEAD"; }
	if (loc.allowPost)   { if (!allow.empty()) allow += ", "; allow += "POST"; }
	if (loc.allowDelete) { if (!allow.empty()) allow += ", "; allow += "DELETE"; }

	return allow;
}

static std::string getContentType(const std::string& p)
{
	std::size_t dot = p.rfind('.');
	if (dot == std::string::npos)
		return "application/octet-stream";

	std::string ext = p.substr(dot + 1);
	if (ext == "html")	return "text/html";
	if (ext == "css")	return "text/css";
	if (ext == "js")	return "application/javascript";
	if (ext == "png")	return "image/png";
	if (ext == "jpg")	return "image/jpeg";
	if (ext == "jpeg")	return "image/jpeg";
	if (ext == "gif")	return "image/gif";
	return "application/octet-stream";
}

static std::string makeUploadFileName()
{
	std::time_t t = std::time(0);
	return "upload_" + std::to_string((long long)t) + ".bin";
}

static const LocationConfig* matchLocation(const ServerConfig& cfg, const std::string& path)
{
	// locations already sorted by prefix length (normalizeAll)
	for (std::size_t i = 0; i < cfg.locations.size(); ++i)
	{
		const LocationConfig& loc = cfg.locations[i];
		const std::string& pre = loc.prefix;

		if (pre == "/")
			return &loc;

		if (path.size() >= pre.size() && path.compare(0, pre.size(), pre) == 0)
		{
			if (path.size() == pre.size())
				return &loc;

			if (!pre.empty() && pre[pre.size() - 1] == '/')
				return &loc;

			if (path[pre.size()] == '/')
				return &loc;
		}
	}
	return 0;
}

// Build relPath relative to server root, taking location prefix into account.
// Important fix: if request is exactly "/uploads" or "/uploads/", map to folder "uploads".
static std::string buildRelPath(const LocationConfig* loc, const std::string& reqPath)
{
	std::string relPath;

	if (!loc)
		return relPath;

	if (loc->prefix == "/")
	{
		if (reqPath.size() > 1)
			relPath = reqPath.substr(1);
		else
			relPath = "";
	}
	else
	{
		if (reqPath.size() > loc->prefix.size())
		{
			relPath = reqPath.substr(loc->prefix.size());
			if (!relPath.empty() && relPath[0] == '/')
				relPath.erase(0, 1);
		}
		else
		{
			relPath = "";
		}

		// If requested exactly the location itself => map to that folder
		if (relPath.empty())
		{
			std::string pre = loc->prefix; // "/uploads"
			if (!pre.empty() && pre[0] == '/')
				pre.erase(0, 1);           // "uploads"
			relPath = pre;
		}
	}

	return relPath;
}

// ---------- main router ----------

HttpResponse HttpRouter::route(const HttpRequest& req, const ServerConfig& cfg)
{
	HttpResponse res;

	res.version = req.version;
	const LocationConfig* loc = matchLocation(cfg, req.path);
	if (!loc)
	{
		HttpError::fill(res, cfg, 500, "Internal Server Error");
		if (req.method == "HEAD")
			res.body = "";
		applyConnectionPolicy(req, res);
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
		res.headers["Allow"] = buildAllowHeader(*loc);

		if (req.method == "HEAD")
			res.body = "";

		applyConnectionPolicy(req, res);
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

		applyConnectionPolicy(req, res);
		return res;
	}

	// ----- build filesystem path using location -----
	std::string baseRoot = cfg.root;
	if (!loc->root.empty())
		baseRoot = loc->root;

	std::string indexName = cfg.index;
	if (!loc->index.empty())
		indexName = loc->index;

	std::string relPath = buildRelPath(loc, req.path);
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
			HttpError::fill(res, cfg, 500, "Internal Server Error");
			if (req.method == "HEAD")
				res.body = "";
			applyConnectionPolicy(req, res);
			return res;
		}

		res.status = 201;
		res.reason = "Created";
		res.body = "Uploaded: " + name + "\n";
		res.headers["Content-Type"] = "text/plain";
		res.headers["Content-Length"] = std::to_string(res.body.size());

		applyConnectionPolicy(req, res);
		return res;
	}

	// ----- DELETE: delete file by fsPath -----
	if (req.method == "DELETE")
	{
		if (FileUtils::isDirectory(fsPath))
		{
			res.status = 403;
			res.reason = "Forbidden";
			res.body = "Cannot delete directory\n";
			res.headers["Content-Type"] = "text/plain";
			res.headers["Content-Length"] = std::to_string(res.body.size());

			applyConnectionPolicy(req, res);
			return res;
		}

		if (::remove(fsPath.c_str()) != 0)
		{
			res.status = 404;
			res.reason = "Not Found";
			res.body = "Not Found\n";
			res.headers["Content-Type"] = "text/plain";
			res.headers["Content-Length"] = std::to_string(res.body.size());

			applyConnectionPolicy(req, res);
			return res;
		}

		res.status = 204;
		res.reason = "No Content";
		res.body = "";
		res.headers["Content-Length"] = "0";

		applyConnectionPolicy(req, res);
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
				res.headers["Content-Type"] = "text/html";
				res.headers["Content-Length"] = std::to_string(html.size());

				// IMPORTANT: HEAD => no body, but same Content-Length as GET
				if (req.method == "HEAD")
					res.body = "";
				else
					res.body = html;

				applyConnectionPolicy(req, res);
				return res;
			}

			HttpError::fill(res, cfg, 403, "Forbidden");
			if (req.method == "HEAD")
				res.body = "";

			applyConnectionPolicy(req, res);
			return res;
		}
	}

	// ----- serve file (GET/HEAD) -----
	std::string body;
	if (!FileUtils::readFile(fsPath, body))
	{
		HttpError::fill(res, cfg, 404, "Not Found");
		// IMPORTANT: HEAD => no body, but keep Content-Length from the error page
		if (req.method == "HEAD")
			res.body = "";

		applyConnectionPolicy(req, res);
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

	applyConnectionPolicy(req, res);
	return res;
}
