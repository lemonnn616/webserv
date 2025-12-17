#include "http/HttpHandler.hpp"
#include "http/HttpRequest.hpp"
#include "http/HttpResponse.hpp"

#include <fstream>
#include <cstdlib>
#include <cstdio>     // remove
#include <sys/stat.h> // stat
#include <dirent.h>   // opendir

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

	return "application/octet-stream";
}

static void sendResponse(
	std::string& out,
	ConnectionState& state,
	int status,
	const std::string& reason,
	const std::string& body,
	const std::string& contentType,
	bool closeAfterWrite
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
	(void)closeAfterWrite;
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

static void sendSimple(
	std::string& out,
	ConnectionState& state,
	int status,
	const std::string& reason,
	const std::string& body
)
{
	sendResponse(out, state, status, reason, body, "text/plain", true);
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
	if (_cfgs == 0 || _cfgs->empty())
	{
		sendSimple(outBuffer, state, 500, "Internal Server Error", "No config\n");
		return;
	}

	const ServerConfig& cfg = (serverConfigIndex < _cfgs->size()) ? (*_cfgs)[serverConfigIndex] : (*_cfgs)[0];

	// ---- 1) headers complete? ----
	std::size_t headersEnd = inBuffer.find("\r\n\r\n");
	if (headersEnd == std::string::npos)
		return;

	std::string headersBlock = inBuffer.substr(0, headersEnd);
	std::string rest = inBuffer.substr(headersEnd + 4);

	// ---- 2) request-line / headers split ----
	std::size_t lineEnd = headersBlock.find("\r\n");
	if (lineEnd == std::string::npos)
	{
		sendSimple(outBuffer, state, 400, "Bad Request", "Bad Request\n");
		return;
	}

	std::string requestLine = headersBlock.substr(0, lineEnd);
	std::string headersPart = headersBlock.substr(lineEnd + 2);

	// ---- 3) request-line ----
	HttpRequest request;

	std::size_t p1 = requestLine.find(' ');
	std::size_t p2;
	if (p1 == std::string::npos)
	{
		sendSimple(outBuffer, state, 400, "Bad Request", "Bad Request\n");
		return;
	}
	p2 = requestLine.find(' ', p1 + 1);
	if (p2 == std::string::npos)
	{
		sendSimple(outBuffer, state, 400, "Bad Request", "Bad Request\n");
		return;
	}

	request.method = requestLine.substr(0, p1);
	request.path = requestLine.substr(p1 + 1, p2 - p1 - 1);
	request.version = requestLine.substr(p2 + 1);

	// ---- 4) headers parse ----
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

	// ---- 5) body (Content-Length only) ----
	std::size_t contentLength = 0;
	if (request.headers.count("Content-Length"))
		contentLength = static_cast<std::size_t>(std::atoi(request.headers["Content-Length"].c_str()));

	if (contentLength > cfg.clientMaxBodySize)
	{
		sendSimple(outBuffer, state, 413, "Payload Too Large", "Payload Too Large\n");
		return;
	}

	if (rest.size() < contentLength)
	{
		// ждём весь body
		return;
	}

	request.body = rest.substr(0, contentLength);
	inBuffer.erase(0, headersEnd + 4 + contentLength);

	// ---- Security: block ../ ----
	if (pathHasDotDot(request.path))
	{
		sendSimple(outBuffer, state, 404, "Not Found", "Not Found\n");
		return;
	}

	// =========================
	// ROUTING
	// =========================

	// 1) POST /upload/<name>  (или /upload -> upload.bin)
	if (request.method == "POST")
	{
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
				sendSimple(outBuffer, state, 500, "Internal Server Error", "Upload failed\n");
				return;
			}

			sendSimple(outBuffer, state, 201, "Created", "Created\n");
			return;
		}

		// если POST не upload — пока 404 (потом сделаешь locations из конфига)
		sendSimple(outBuffer, state, 404, "Not Found", "Not Found\n");
		return;
	}

	// 2) DELETE /path/file
	if (request.method == "DELETE")
	{
		if (request.path.empty() || request.path[0] != '/')
		{
			sendSimple(outBuffer, state, 400, "Bad Request", "Bad Request\n");
			return;
		}

		std::string rel = request.path.substr(1);
		std::string fsPath = joinPath(cfg.root, rel);

		if (!fileExists(fsPath))
		{
			sendSimple(outBuffer, state, 404, "Not Found", "Not Found\n");
			return;
		}

		if (isDirectory(fsPath))
		{
			sendSimple(outBuffer, state, 403, "Forbidden", "Forbidden\n");
			return;
		}

		// В 42 обычно можно unlink(). Если у вас запрещено — скажи, заменим строго под список.
		if (std::remove(fsPath.c_str()) != 0)
		{
			sendSimple(outBuffer, state, 403, "Forbidden", "Forbidden\n");
			return;
		}

		// 204 без тела
		sendResponseNoBody(outBuffer, state, 204, "No Content", "text/plain", 0);
		return;
	}

	// 3) GET/HEAD static
	if (request.method != "GET" && request.method != "HEAD")
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

	// map URL -> fs
	std::string rel;
	if (request.path == "/")
		rel = cfg.index;
	else
		rel = request.path.substr(1);

	std::string fsPath = joinPath(cfg.root, rel);

	// если это директория — пробуем index
	if (isDirectory(fsPath))
	{
		fsPath = joinPath(fsPath, cfg.index);
	}

	std::string body;
	if (!readFile(fsPath, body))
	{
		sendSimple(outBuffer, state, 404, "Not Found", "Not Found\n");
		return;
	}

	std::string ct = getContentType(fsPath);

	if (request.method == "HEAD")
	{
		sendResponseNoBody(outBuffer, state, 200, "OK", ct, body.size());
		return;
	}

	sendResponse(outBuffer, state, 200, "OK", body, ct, true);
}
