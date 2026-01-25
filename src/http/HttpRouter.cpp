#include "http/HttpRouter.hpp"
#include "http/HttpError.hpp"
#include "utils/FileUtils.hpp"
#include "http/AutoIndex.hpp"
#include "cgi/CgiRunner.hpp"

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
	return p.substr(dot); // ".py"
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

	// root location: "/a/b" -> "a/b"
	if (loc->prefix == "/")
	{
		if (reqPath.size() > 1)
			return reqPath.substr(1);
		return "";
	}

	// normalize prefix folder name: "/cgi-bin" -> "cgi-bin"
	std::string prefixFolder = loc->prefix;
	if (!prefixFolder.empty() && prefixFolder[0] == '/')
		prefixFolder.erase(0, 1);

	// remainder after location prefix
	std::string rest;
	if (reqPath.size() > loc->prefix.size())
	{
		rest = reqPath.substr(loc->prefix.size()); // starts with "/hello.py"
		if (!rest.empty() && rest[0] == '/')
			rest.erase(0, 1); // "hello.py"
	}
	else
	{
		rest = "";
	}

	// If loc.root is set, assume it already points to directory for this location
	// => use only remainder
	if (!loc->root.empty())
		return rest;

	// Default: location maps to folder under server root
	// => "cgi-bin" + "/" + "hello.py"
	if (rest.empty())
		return prefixFolder;

	return prefixFolder + "/" + rest;
}

// --- CGI stdout parsing ---
// CGI typically outputs:
//   Status: 200 OK\r\n (optional)
//   Header: value\r\n
//   \r\n
//   body...
static bool parseCgiOutput(const std::string& out, HttpResponse& res)
{
	std::size_t sep = out.find("\r\n\r\n");
	if (sep == std::string::npos)
	{
		// allow LF-only (some scripts do \n)
		sep = out.find("\n\n");
		if (sep == std::string::npos)
			return false;
	}

	std::string head = out.substr(0, sep);
	std::string body;
	if (out.size() > sep)
	{
		// if \r\n\r\n -> +4, if \n\n -> +2
		if (out.compare(sep, 4, "\r\n\r\n") == 0)
			body = out.substr(sep + 4);
		else
			body = out.substr(sep + 2);
	}

	// defaults
	res.status = 200;
	res.reason = "OK";
	res.body = body;

	// split lines
	std::size_t pos = 0;
	while (pos < head.size())
	{
		std::size_t eol = head.find("\n", pos);
		std::string line;
		if (eol == std::string::npos)
		{
			line = head.substr(pos);
			pos = head.size();
		}
		else
		{
			line = head.substr(pos, eol - pos);
			pos = eol + 1;
		}

		// trim trailing '\r'
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);

		if (line.empty())
			continue;

		std::size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;

		std::string key = line.substr(0, colon);
		std::string val = line.substr(colon + 1);

		// ltrim spaces
		while (!val.empty() && (val[0] == ' ' || val[0] == '\t'))
			val.erase(0, 1);

		std::string keyLower = toLowerStr(key);

		if (keyLower == "status")
		{
			// "200 OK"
			int code = std::atoi(val.c_str());
			if (code > 0)
				res.status = code;

			std::size_t sp = val.find(' ');
			if (sp != std::string::npos && sp + 1 < val.size())
				res.reason = val.substr(sp + 1);
			else
				res.reason = "OK";
		}
		else
		{
			// CGI headers -> HTTP headers
			res.headers[key] = val;
		}
	}

	// Content-Length: if not provided, set from body
	if (res.headers.find("Content-Length") == res.headers.end())
		res.headers["Content-Length"] = std::to_string(res.body.size());

	// If no Content-Type, assume text/plain
	if (res.headers.find("Content-Type") == res.headers.end())
		res.headers["Content-Type"] = "text/plain";

	return true;
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
	if (req.method == "GET" && loc->allowGet) methodAllowed = true;
	else if (req.method == "HEAD" && loc->allowHead) methodAllowed = true;
	else if (req.method == "POST" && loc->allowPost) methodAllowed = true;
	else if (req.method == "DELETE" && loc->allowDelete) methodAllowed = true;

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
		if (code <= 0) code = 302;

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

	// =========================
	// CGI (by extension in cfg.cgi)
	// =========================
	{
		std::string ext = getExtWithDot(fsPath); // ".py"
		std::map<std::string, std::string>::const_iterator it = cfg.cgi.find(ext);
		if (!ext.empty() && it != cfg.cgi.end())
		{
			// must exist and not be directory
			if (!FileUtils::exists(fsPath) || FileUtils::isDirectory(fsPath))
			{
				HttpError::fill(res, cfg, 404, "Not Found");
				if (req.method == "HEAD")
					res.body = "";
				applyConnectionPolicy(req, res);
				return res;
			}

			CgiRunner::Result cg;
			std::map<std::string, std::string> extra; // later you can add SCRIPT_ROOT etc.

			bool ok = CgiRunner::run(it->second, fsPath, req, extra, cg);
			if (!ok || cg.stdoutData.empty())
			{
				// if stderr not empty -> helpful for debug
				HttpError::fill(res, cfg, 502, "Bad Gateway");
				applyConnectionPolicy(req, res);
				return res;
			}

			HttpResponse cgiRes;
			cgiRes.version = req.version;

			if (!parseCgiOutput(cg.stdoutData, cgiRes))
			{
				HttpError::fill(res, cfg, 502, "Bad Gateway");
				applyConnectionPolicy(req, res);
				return res;
			}

			// HEAD: same headers, empty body
			if (req.method == "HEAD")
				cgiRes.body = "";

			applyConnectionPolicy(req, cgiRes);
			return cgiRes;
		}
	}

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
			res.headers["Connection"] = "close";
			return res;
		}

		if (::remove(fsPath.c_str()) != 0)
		{
			// distinguish errors
			if (errno == ENOENT)
			{
				res.status = 404;
				res.reason = "Not Found";
				res.body = "Not Found\n";
				res.headers["Content-Type"] = "text/plain";
				res.headers["Content-Length"] = std::to_string(res.body.size());
				res.headers["Connection"] = "close";
				return res;
			}
			if (errno == EACCES || errno == EPERM)
			{
				res.status = 403;
				res.reason = "Forbidden";
				res.body = "Forbidden\n";
				res.headers["Content-Type"] = "text/plain";
				res.headers["Content-Length"] = std::to_string(res.body.size());
				res.headers["Connection"] = "close";
				return res;
			}

			HttpError::fill(res, cfg, 500, "Internal Server Error");
			if (req.method == "HEAD")
				res.body = "";
			res.headers["Connection"] = "close";
			return res;
		}

		res.status = 204;
		res.reason = "No Content";
		res.body = "";
		res.headers["Content-Length"] = "0";
		res.headers["Connection"] = "close";
		return res;
	}


// ----- serve file (GET/HEAD) -----

	// If path exists and is directory -> redirect/index/autoindex
	if (FileUtils::exists(fsPath) && FileUtils::isDirectory(fsPath))
	{
		// Redirect: "/uploads" -> "/uploads/"
		if (!req.path.empty() && req.path[req.path.size() - 1] != '/')
		{
			res.status = 301;
			res.reason = "Moved Permanently";
			res.headers["Location"] = req.path + "/";
			res.body = "";
			res.headers["Content-Length"] = "0";

			if (req.method == "HEAD")
				res.body = "";

			applyConnectionPolicy(req, res);
			return res;
		}

		// Try index inside directory
		{
			std::string indexPath = FileUtils::join(fsPath, indexName);
			std::string indexBody;

			if (FileUtils::readFile(indexPath, indexBody))
			{
				res.status = 200;
				res.reason = "OK";
				res.headers["Content-Type"] = getContentTypeByPath(indexPath);
				res.headers["Content-Length"] = std::to_string(indexBody.size());

				if (req.method == "GET")
					res.body = indexBody;
				else
					res.body = "";

				applyConnectionPolicy(req, res);
				return res;
			}
		}

		// Autoindex if enabled
		if (loc->autoindex)
		{
			std::string html = AutoIndex::generate(req.path, fsPath);
			if (!html.empty())
			{
				res.status = 200;
				res.reason = "OK";
				res.headers["Content-Type"] = "text/html";
				res.headers["Content-Length"] = std::to_string(html.size());

				if (req.method == "GET")
					res.body = html;
				else
					res.body = "";

				applyConnectionPolicy(req, res);
				return res;
			}
		}

		// Directory but no index and autoindex off -> 404
		HttpError::fill(res, cfg, 404, "Not Found");
		if (req.method == "HEAD")
			res.body = "";
		applyConnectionPolicy(req, res);
		return res;
	}

	// Normal file
	{
		std::string body;

		if (!FileUtils::readFile(fsPath, body))
		{
			HttpError::fill(res, cfg, 404, "Not Found");
			if (req.method == "HEAD")
				res.body = "";
			applyConnectionPolicy(req, res);
			return res;
		}

		res.status = 200;
		res.reason = "OK";
		res.headers["Content-Type"] = getContentTypeByPath(fsPath);
		res.headers["Content-Length"] = std::to_string(body.size());

		if (req.method == "GET")
			res.body = body;
		else
			res.body = "";

		applyConnectionPolicy(req, res);
		return res;
	}

