#include "http/HttpHandler.hpp"
#include "http/HttpRequest.hpp"
#include "http/HttpResponse.hpp"

#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <sys/stat.h>
#include <dirent.h>

// ================== ctor / config ==================

HttpHandler::HttpHandler()
	: _cfgs(0)
{
}

HttpHandler::~HttpHandler()
{
}

void HttpHandler::setServerConfigs(const std::vector<ServerConfig>* cfgs)
{
	_cfgs = cfgs;
}

// ================== small utils ==================

static std::string trimLeftOneSpace(const std::string& s)
{
	if (!s.empty() && s[0] == ' ')
		return s.substr(1);
	return s;
}

static bool readFile(const std::string& path, std::string& out)
{
	std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
	if (!file)
		return false;

	out.assign(
		(std::istreambuf_iterator<char>(file)),
		std::istreambuf_iterator<char>()
	);
	return true;
}

static bool writeFile(const std::string& path, const std::string& data)
{
	std::ofstream file(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!file)
		return false;

	file.write(data.data(), static_cast<std::streamsize>(data.size()));
	return file.good();
}

static bool pathHasDotDot(const std::string& p)
{
	return (p.find("..") != std::string::npos);
}

static bool isDirectory(const std::string& fsPath)
{
	DIR* d = opendir(fsPath.c_str());
	if (d)
	{
		closedir(d);
		return true;
	}
	return false;
}

static bool fileExists(const std::string& fsPath)
{
	struct stat st;
	return (stat(fsPath.c_str(), &st) == 0);
}

static std::string joinPath(const std::string& a, const std::string& b)
{
	if (a.empty())
		return b;
	if (!a.empty() && a[a.size() - 1] == '/')
		return a + b;
	return a + "/" + b;
}

static std::string getContentType(const std::string& path)
{
	std::size_t dot = path.rfind('.');
	if (dot == std::string::npos)
		return "application/octet-stream";

	std::string ext = path.substr(dot + 1);

	if (ext == "html")
		return "text/html";
	if (ext == "css")
		return "text/css";
	if (ext == "js")
		return "application/javascript";
	if (ext == "png")
		return "image/png";
	if (ext == "jpg" || ext == "jpeg")
		return "image/jpeg";
	if (ext == "txt")
		return "text/plain";

	return "application/octet-stream";
}

static void sendResponse(
	std::string& out,
	ConnectionState& state,
	int status,
	const std::string& reason,
	const std::string& body,
	const std::string& contentType
)
{
	HttpResponse res;
	res.status = status;
	res.reason = reason;
	res.body = body;

	res.headers["Content-Length"] = std::to_string(body.size());
	res.headers["Content-Type"] = contentType;
	res.headers["Connection"] = "close";

	out = res.serialize();
	state = ConnectionState::WRITING;
}

static void sendResponseNoBody(
	std::string& out,
	ConnectionState& state,
	int status,
	const std::string& reason,
	const std::string& contentType,
	std::size_t contentLength
)
{
	HttpResponse res;
	res.status = status;
	res.reason = reason;
	res.body = "";

	res.headers["Content-Length"] = std::to_string(contentLength);
	res.headers["Content-Type"] = contentType;
	res.headers["Connection"] = "close";

	out = res.serialize();
	state = ConnectionState::WRITING;
}

static std::string defaultErrorBody(int status, const std::string& reason)
{
	std::string b;
	b += "<!doctype html><html><head><meta charset=\"utf-8\"></head><body>";
	b += "<h1>";
	b += std::to_string(status);
	b += " ";
	b += reason;
	b += "</h1>";
	b += "</body></html>\n";
	return b;
}

static void sendErrorPage(
	std::string& out,
	ConnectionState& state,
	const ServerConfig& cfg,
	int status,
	const std::string& reason
)
{
	std::string body;

	std::map<int, std::string>::const_iterator it = cfg.errorPages.find(status);
	if (it != cfg.errorPages.end())
	{
		// errorPages paths считаем относительными от server.root
		std::string fsPath = joinPath(cfg.root, it->second);
		if (!readFile(fsPath, body))
			body = defaultErrorBody(status, reason);
	}
	else
	{
		body = defaultErrorBody(status, reason);
	}

	sendResponse(out, state, status, reason, body, "text/html");
}

static bool isMethodAllowed(const LocationConfig& loc, const std::string& method)
{
	if (method == "GET") return loc.allowGet;
	if (method == "HEAD") return loc.allowHead;
	if (method == "POST") return loc.allowPost;
	if (method == "DELETE") return loc.allowDelete;
	return false;
}

// самый важный матчинг: выбираем location с самым длинным prefix
static const LocationConfig& matchLocation(const ServerConfig& cfg, const std::string& path)
{
	std::size_t bestLen = 0;
	std::size_t bestIdx = 0;

	for (std::size_t i = 0; i < cfg.locations.size(); ++i)
	{
		const std::string& pfx = cfg.locations[i].prefix;
		if (pfx.empty())
			continue;

		if (path.size() >= pfx.size() && path.compare(0, pfx.size(), pfx) == 0)
		{
			// важно: prefix должен совпадать по границе сегмента
			// "/img" не должен матчить "/imagesX"
			if (path.size() > pfx.size())
			{
				if (pfx[pfx.size() - 1] != '/' && path[pfx.size()] != '/')
					continue;
			}

			if (pfx.size() > bestLen)
			{
				bestLen = pfx.size();
				bestIdx = i;
			}
		}
	}

	return cfg.locations[bestIdx];
}

static std::string makeAutoindexHtml(const std::string& urlPath, const std::string& fsDir)
{
	DIR* d = opendir(fsDir.c_str());
	if (!d)
		return "";

	std::string html;
	html += "<!doctype html><html><head><meta charset=\"utf-8\"></head><body>";
	html += "<h1>Index of ";
	html += urlPath;
	html += "</h1><ul>";

	struct dirent* ent;
	while ((ent = readdir(d)) != 0)
	{
		std::string name(ent->d_name);
		if (name == "." || name == "..")
			continue;

		html += "<li><a href=\"";
		// аккуратно: если urlPath не заканчивается '/', добавим
		if (!urlPath.empty() && urlPath[urlPath.size() - 1] == '/')
			html += urlPath + name;
		else
			html += urlPath + "/" + name;

		html += "\">";
		html += name;
		html += "</a></li>";
	}

	html += "</ul></body></html>\n";
	closedir(d);
	return html;
}

// URL -> относительный путь (без ведущего '/')
static std::string stripLeadingSlash(const std::string& p)
{
	if (!p.empty() && p[0] == '/')
		return p.substr(1);
	return p;
}

// ================== main handler ==================

void HttpHandler::onDataReceived(
	int,
	std::string& inBuffer,
	std::string& outBuffer,
	ConnectionState& state,
	std::size_t serverConfigIndex,
	std::string&
)
{
	if (_cfgs == 0 || _cfgs->empty())
	{
		sendResponse(outBuffer, state, 500, "Internal Server Error", "No config\n", "text/plain");
		return;
	}

	const ServerConfig& cfg =
		(serverConfigIndex < _cfgs->size()) ? (*_cfgs)[serverConfigIndex] : (*_cfgs)[0];

	// ---------- 1) headers complete? ----------
	std::size_t headersEnd = inBuffer.find("\r\n\r\n");
	if (headersEnd == std::string::npos)
		return;

	std::string headersBlock = inBuffer.substr(0, headersEnd);
	std::string rest = inBuffer.substr(headersEnd + 4);

	// ---------- 2) request-line / headers split ----------
	std::size_t lineEnd = headersBlock.find("\r\n");
	if (lineEnd == std::string::npos)
	{
		sendErrorPage(outBuffer, state, cfg, 400, "Bad Request");
		return;
	}

	std::string requestLine = headersBlock.substr(0, lineEnd);
	std::string headersPart = headersBlock.substr(lineEnd + 2);

	// ---------- 3) request-line ----------
	HttpRequest request;

	std::size_t p1 = requestLine.find(' ');
	if (p1 == std::string::npos)
	{
		sendErrorPage(outBuffer, state, cfg, 400, "Bad Request");
		return;
	}
	std::size_t p2 = requestLine.find(' ', p1 + 1);
	if (p2 == std::string::npos)
	{
		sendErrorPage(outBuffer, state, cfg, 400, "Bad Request");
		return;
	}

	request.method = requestLine.substr(0, p1);
	request.path = requestLine.substr(p1 + 1, p2 - p1 - 1);
	request.version = requestLine.substr(p2 + 1);

	// минимальная sanity-проверка
	if (request.path.empty() || request.path[0] != '/')
	{
		sendErrorPage(outBuffer, state, cfg, 400, "Bad Request");
		return;
	}

	// ---------- 4) headers parse ----------
	std::size_t pos = 0;
	while (pos < headersPart.size())
	{
		std::size_t end = headersPart.find("\r\n", pos);
		if (end == std::string::npos)
			break;

		std::string line = headersPart.substr(pos, end - pos);
		pos = end + 2;

		if (line.empty())
			break;

		std::size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;

		std::string key = line.substr(0, colon);
		std::string value = trimLeftOneSpace(line.substr(colon + 1));
		request.headers[key] = value;
	}

	// ---------- 5) body (Content-Length only) ----------
	std::size_t contentLength = 0;
	if (request.headers.count("Content-Length"))
		contentLength = static_cast<std::size_t>(std::atoi(request.headers["Content-Length"].c_str()));

	if (contentLength > cfg.clientMaxBodySize)
	{
		sendErrorPage(outBuffer, state, cfg, 413, "Payload Too Large");
		return;
	}

	if (rest.size() < contentLength)
		return; // ждём весь body

	request.body = rest.substr(0, contentLength);
	inBuffer.erase(0, headersEnd + 4 + contentLength);

	// ---------- security ----------
	if (pathHasDotDot(request.path))
	{
		sendErrorPage(outBuffer, state, cfg, 404, "Not Found");
		return;
	}

	// =========================
	// ROUTING by location
	// =========================

	const LocationConfig& loc = matchLocation(cfg, request.path);

	if (!isMethodAllowed(loc, request.method))
	{
		HttpResponse res;
		res.status = 405;
		res.reason = "Method Not Allowed";
		res.body = "Method Not Allowed\n";
		res.headers["Content-Length"] = "19";
		res.headers["Content-Type"] = "text/plain";
		res.headers["Connection"] = "close";
		res.headers["Allow"] = "GET, HEAD, POST, DELETE";
		outBuffer = res.serialize();
		state = ConnectionState::WRITING;
		return;
	}

	// =========================
	// POST upload (временно: без конфиг-правил, потом привяжем к location)
	// =========================
	if (request.method == "POST")
	{
		// простой протокол: POST /upload/<name>
		std::string prefix = "/upload";
		if (request.path == prefix || request.path.find(prefix + "/") == 0)
		{
			std::string name = "upload.bin";
			if (request.path.find(prefix + "/") == 0)
			{
				name = request.path.substr(prefix.size() + 1);
				if (name.empty())
					name = "upload.bin";
			}

			std::string fsPath = joinPath(cfg.uploadDir, name);
			if (!writeFile(fsPath, request.body))
			{
				sendErrorPage(outBuffer, state, cfg, 500, "Internal Server Error");
				return;
			}

			sendResponse(outBuffer, state, 201, "Created", "Created\n", "text/plain");
			return;
		}

		sendErrorPage(outBuffer, state, cfg, 404, "Not Found");
		return;
	}

	// =========================
	// DELETE static file
	// =========================
	if (request.method == "DELETE")
	{
		std::string rel = stripLeadingSlash(request.path);
		std::string base = joinPath(cfg.root, loc.root);
		std::string fsPath = joinPath(base, rel);

		if (!fileExists(fsPath))
		{
			sendErrorPage(outBuffer, state, cfg, 404, "Not Found");
			return;
		}
		if (isDirectory(fsPath))
		{
			sendErrorPage(outBuffer, state, cfg, 403, "Forbidden");
			return;
		}

		if (std::remove(fsPath.c_str()) != 0)
		{
			sendErrorPage(outBuffer, state, cfg, 403, "Forbidden");
			return;
		}

		sendResponseNoBody(outBuffer, state, 204, "No Content", "text/plain", 0);
		return;
	}

	// =========================
	// GET/HEAD static
	// =========================
	if (request.method != "GET" && request.method != "HEAD")
	{
		sendErrorPage(outBuffer, state, cfg, 405, "Method Not Allowed");
		return;
	}

	// URL -> fs
	std::string relUrl = stripLeadingSlash(request.path);

	// base: server.root + location.root
	std::string base = joinPath(cfg.root, loc.root);

	// если запрос "/" → индекс
	std::string fsPath;
	if (request.path == "/")
		fsPath = joinPath(base, loc.index);
	else
		fsPath = joinPath(base, relUrl);

	// если это директория
	if (isDirectory(fsPath))
	{
		std::string indexPath = joinPath(fsPath, loc.index);

		if (fileExists(indexPath))
		{
			fsPath = indexPath;
		}
		else if (loc.autoindex)
		{
			std::string html = makeAutoindexHtml(request.path, fsPath);
			if (html.empty())
			{
				sendErrorPage(outBuffer, state, cfg, 403, "Forbidden");
				return;
			}

			if (request.method == "HEAD")
				sendResponseNoBody(outBuffer, state, 200, "OK", "text/html", html.size());
			else
				sendResponse(outBuffer, state, 200, "OK", html, "text/html");
			return;
		}
		else
		{
			sendErrorPage(outBuffer, state, cfg, 403, "Forbidden");
			return;
		}
	}

	// читаем файл
	std::string body;
	if (!readFile(fsPath, body))
	{
		sendErrorPage(outBuffer, state, cfg, 404, "Not Found");
		return;
	}

	std::string ct = getContentType(fsPath);

	if (request.method == "HEAD")
	{
		sendResponseNoBody(outBuffer, state, 200, "OK", ct, body.size());
		return;
	}

	sendResponse(outBuffer, state, 200, "OK", body, ct);
}
