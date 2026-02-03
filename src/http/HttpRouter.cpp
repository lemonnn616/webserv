#include "http/HttpRouter.hpp"
#include "http/HttpError.hpp"
#include "utils/FileUtils.hpp"
#include "http/AutoIndex.hpp"

#include <unistd.h>
#include <ctime>
#include <cstdio>
#include <string>
#include <cctype>
#include <cstdlib>

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
	std::string raw = "";
	std::map<std::string, std::string>::const_iterator it = req.headers.find("connection");
	if (it != req.headers.end())
		raw = it->second;

	std::string c = toLowerStr(raw);

	if (req.version == "HTTP/1.0")
	{
		if (c.find("keep-alive") != std::string::npos)
			res.headers["Connection"] = "keep-alive";
		else
			res.headers["Connection"] = "close";
	}
	else
	{
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

static std::string getExtWithDot(const std::string& p)
{
	std::size_t dot = p.rfind('.');
	if (dot == std::string::npos)
		return "";
	return p.substr(dot); // includes '.'
}

static std::string getContentTypeByPath(const std::string& p)
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
	if (ext == "txt")	return "text/plain";
	return "application/octet-stream";
}

static std::string makeUploadFileName()
{
	static unsigned long counter = 0;
	std::time_t t = std::time(0);
	long pid = (long)::getpid();
	++counter;

	return "upload_" + std::to_string((long long)t)
		+ "_" + std::to_string(pid)
		+ "_" + std::to_string((unsigned long long)counter)
		+ ".bin";
}

static const LocationConfig* matchLocation(const ServerConfig& cfg, const std::string& path)
{
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

static std::string buildRelPath(const LocationConfig* loc, const std::string& reqPath)
{
	std::string rel;

	if (!loc)
		return rel;

	// if location is "/", reqPath is "/x/y" -> rel = "x/y"
	if (loc->prefix == "/")
	{
		if (reqPath.size() > 1)
			return reqPath.substr(1);
		return "";
	}

	// for location "/directory", reqPath "/directory/abc" -> rel = "abc"
	// for location "/directory", reqPath "/directory" -> rel = "" (directory root)
	std::string rest;
	if (reqPath.size() > loc->prefix.size())
	{
		rest = reqPath.substr(loc->prefix.size());
		if (!rest.empty() && rest[0] == '/')
			rest.erase(0, 1);
	}
	else
	{
		rest = "";
	}

	return rest;
}

static bool isMethodAllowed(const HttpRequest& req, const LocationConfig& loc)
{
	if (req.method == "GET")
		return loc.allowGet;
	if (req.method == "HEAD")
		return loc.allowHead;
	if (req.method == "POST")
		return loc.allowPost;
	if (req.method == "DELETE")
		return loc.allowDelete;
	return false;
}

// ---------- router ----------

HttpRouter::RouteResult HttpRouter::route2(const HttpRequest& req, const ServerConfig& cfg)
{
	RouteResult rr;
	rr.response.version = req.version;

	const LocationConfig* loc = matchLocation(cfg, req.path);
	if (!loc)
	{
		HttpError::fill(rr.response, cfg, 500, "Internal Server Error");
		if (req.method == "HEAD")
			rr.response.body = "";
		applyConnectionPolicy(req, rr.response);
		return rr;
	}

	// ----- method allowed? -----
	if (!isMethodAllowed(req, *loc))
	{
		rr.response.status = 405;
		rr.response.reason = "Method Not Allowed";
		rr.response.body = "Method Not Allowed\n";
		rr.response.headers["Content-Type"] = "text/plain";
		rr.response.headers["Allow"] = buildAllowHeader(*loc);
		rr.response.headers["Content-Length"] = std::to_string(rr.response.body.size());
		if (req.method == "HEAD")
			rr.response.body = "";
		applyConnectionPolicy(req, rr.response);
		return rr;
	}

	// ----- redirect/return -----
	if (loc->hasReturn)
	{
		int code = loc->returnCode;
		if (code <= 0)
			code = 302;

		rr.response.status = code;
		rr.response.reason = "Found";
		rr.response.headers["Location"] = loc->returnUrl;
		rr.response.body = "";
		rr.response.headers["Content-Length"] = "0";
		applyConnectionPolicy(req, rr.response);
		return rr;
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

	// =========================
	// CGI detect (tester): only POST + ext in cfg.cgi
	// =========================
	if (req.method == "POST")
	{
		std::string ext = getExtWithDot(fsPath);
		std::map<std::string, std::string>::const_iterator it = cfg.cgi.find(ext);

		if (!ext.empty() && it != cfg.cgi.end())
		{
			if (!FileUtils::exists(fsPath) || FileUtils::isDirectory(fsPath))
			{
				HttpError::fill(rr.response, cfg, 404, "Not Found");
				if (req.method == "HEAD")
					rr.response.body = "";
				applyConnectionPolicy(req, rr.response);
				return rr;
			}

			rr.isCgi = true;
			rr.cgiInterpreter = it->second;
			rr.cgiScriptPath = fsPath;
			return rr;
		}
	}

	// ----- POST non-CGI: upload -----
	if (req.method == "POST")
	{
		std::string dir = cfg.uploadDir;
		if (dir.empty())
			dir = "www/uploads";

		std::string name = makeUploadFileName();
		std::string full = FileUtils::join(dir, name);

		if (!FileUtils::writeFile(full, req.body))
		{
			HttpError::fill(rr.response, cfg, 500, "Internal Server Error");
			if (req.method == "HEAD")
				rr.response.body = "";
			applyConnectionPolicy(req, rr.response);
			return rr;
		}

		rr.response.status = 201;
		rr.response.reason = "Created";
		rr.response.body = "Uploaded: " + name + "\n";
		rr.response.headers["Content-Type"] = "text/plain";
		rr.response.headers["Content-Length"] = std::to_string(rr.response.body.size());
		applyConnectionPolicy(req, rr.response);
		return rr;
	}

	// ----- DELETE -----
	if (req.method == "DELETE")
	{
		if (FileUtils::isDirectory(fsPath))
		{
			rr.response.status = 403;
			rr.response.reason = "Forbidden";
			rr.response.body = "Cannot delete directory\n";
			rr.response.headers["Content-Type"] = "text/plain";
			rr.response.headers["Content-Length"] = std::to_string(rr.response.body.size());
			rr.response.headers["Connection"] = "close";
			return rr;
		}

		if (::remove(fsPath.c_str()) != 0)
		{
			if (errno == ENOENT)
			{
				rr.response.status = 404;
				rr.response.reason = "Not Found";
				rr.response.body = "Not Found\n";
				rr.response.headers["Content-Type"] = "text/plain";
				rr.response.headers["Content-Length"] = std::to_string(rr.response.body.size());
				rr.response.headers["Connection"] = "close";
				return rr;
			}
			if (errno == EACCES || errno == EPERM)
			{
				rr.response.status = 403;
				rr.response.reason = "Forbidden";
				rr.response.body = "Forbidden\n";
				rr.response.headers["Content-Type"] = "text/plain";
				rr.response.headers["Content-Length"] = std::to_string(rr.response.body.size());
				rr.response.headers["Connection"] = "close";
				return rr;
			}

			HttpError::fill(rr.response, cfg, 500, "Internal Server Error");
			if (req.method == "HEAD")
				rr.response.body = "";
			rr.response.headers["Connection"] = "close";
			return rr;
		}

		rr.response.status = 204;
		rr.response.reason = "No Content";
		rr.response.body = "";
		rr.response.headers["Content-Length"] = "0";
		rr.response.headers["Connection"] = "close";
		return rr;
	}

	if (FileUtils::exists(fsPath) && FileUtils::isDirectory(fsPath))
	{
		// redirect /dir -> /dir/
		if (!req.path.empty() && req.path[req.path.size() - 1] != '/')
		{
			rr.response.status = 301;
			rr.response.reason = "Moved Permanently";
			rr.response.headers["Location"] = req.path + "/";
			rr.response.body = "";
			rr.response.headers["Content-Length"] = "0";
			if (req.method == "HEAD")
				rr.response.body = "";
			applyConnectionPolicy(req, rr.response);
			return rr;
		}

		{
			std::string indexPath = FileUtils::join(fsPath, indexName);
			std::string indexBody;

			if (FileUtils::readFile(indexPath, indexBody))
			{
				rr.response.status = 200;
				rr.response.reason = "OK";
				rr.response.headers["Content-Type"] = getContentTypeByPath(indexPath);
				rr.response.headers["Content-Length"] = std::to_string(indexBody.size());
				rr.response.body = (req.method == "GET") ? indexBody : "";
				applyConnectionPolicy(req, rr.response);
				return rr;
			}
		}

		// autoindex
		if (loc->autoindex)
		{
			std::string html = AutoIndex::generate(req.path, fsPath);
			if (!html.empty())
			{
				rr.response.status = 200;
				rr.response.reason = "OK";
				rr.response.headers["Content-Type"] = "text/html";
				rr.response.headers["Content-Length"] = std::to_string(html.size());
				rr.response.body = (req.method == "GET") ? html : "";
				applyConnectionPolicy(req, rr.response);
				return rr;
			}
		}

		HttpError::fill(rr.response, cfg, 404, "Not Found");
		if (req.method == "HEAD")
			rr.response.body = "";
		applyConnectionPolicy(req, rr.response);
		return rr;
	}

	// file
	{
		std::string body;

		if (!FileUtils::readFile(fsPath, body))
		{
			HttpError::fill(rr.response, cfg, 404, "Not Found");
			if (req.method == "HEAD")
				rr.response.body = "";
			applyConnectionPolicy(req, rr.response);
			return rr;
		}

		rr.response.status = 200;
		rr.response.reason = "OK";
		rr.response.headers["Content-Type"] = getContentTypeByPath(fsPath);
		rr.response.headers["Content-Length"] = std::to_string(body.size());
		rr.response.body = (req.method == "GET") ? body : "";
		applyConnectionPolicy(req, rr.response);
		return rr;
	}
}


HttpResponse HttpRouter::route(const HttpRequest& req, const ServerConfig& cfg)
{
	RouteResult rr = route2(req, cfg);

	if (rr.isCgi)
	{
		HttpResponse res;
		res.version = req.version;
		HttpError::fill(res, cfg, 500, "Internal Server Error");
		if (req.method == "HEAD")
			res.body = "";
		applyConnectionPolicy(req, res);
		return res;
	}

	return rr.response;
}
