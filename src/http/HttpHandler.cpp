#include "http/HttpHandler.hpp"
#include "http/HttpRequest.hpp"
#include "http/HttpResponse.hpp"

#include <fstream>
#include <cstdlib>

// ================== ctor / dtor ==================

HttpHandler::HttpHandler() {}
HttpHandler::~HttpHandler() {}

// ================== helpers ==================

static std::string getContentType(const std::string& path)
{
	size_t dot = path.rfind('.');
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

static void sendResponse(
	std::string& out,
	ConnectionState& state,
	int status,
	const std::string& reason,
	const std::string& body,
	const std::string& contentType = "text/plain"
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

// ================== main handler ==================

void HttpHandler::onDataReceived(
	int,
	std::string& inBuffer,
	std::string& outBuffer,
	ConnectionState& state,
	std::size_t,
	std::string&
)
{
	// ---------- 1. Headers ----------
	size_t headersEnd = inBuffer.find("\r\n\r\n");
	if (headersEnd == std::string::npos)
		return;

	std::string headersBlock = inBuffer.substr(0, headersEnd);
	std::string rest = inBuffer.substr(headersEnd + 4);

	size_t lineEnd = headersBlock.find("\r\n");
	if (lineEnd == std::string::npos)
	{
		sendResponse(outBuffer, state, 400, "Bad Request", "Bad Request\n");
		return;
	}

	std::string requestLine = headersBlock.substr(0, lineEnd);
	std::string headersPart = headersBlock.substr(lineEnd + 2);

	HttpRequest request;

	// ---------- request-line ----------
	size_t p1 = requestLine.find(' ');
	size_t p2 = requestLine.find(' ', p1 + 1);
	if (p1 == std::string::npos || p2 == std::string::npos)
	{
		sendResponse(outBuffer, state, 400, "Bad Request", "Bad Request\n");
		return;
	}

	request.method = requestLine.substr(0, p1);
	request.path = requestLine.substr(p1 + 1, p2 - p1 - 1);
	request.version = requestLine.substr(p2 + 1);

	// ---------- headers ----------
	size_t pos = 0;
	while (pos < headersPart.size())
	{
		size_t end = headersPart.find("\r\n", pos);
		if (end == std::string::npos)
			break;

		std::string line = headersPart.substr(pos, end - pos);
		pos = end + 2;

		if (line.empty())
			break;

		size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;

		std::string key = line.substr(0, colon);
		std::string value = line.substr(colon + 1);
		if (!value.empty() && value[0] == ' ')
			value.erase(0, 1);

		request.headers[key] = value;
	}

	// ---------- body ----------
	size_t contentLength = 0;
	if (request.headers.count("Content-Length"))
		contentLength = std::atoi(request.headers["Content-Length"].c_str());

	if (rest.size() < contentLength)
	{
		// ждём body полностью
		return;
	}

	request.body = rest.substr(0, contentLength);
	inBuffer.erase(0, headersEnd + 4 + contentLength);

	// ================== ROUTING ==================

	// ---- GET / ----
	if (request.method == "GET" && request.path == "/")
	{
		std::string body;
		if (!readFile("www/index.html", body))
		{
			sendResponse(outBuffer, state, 404, "Not Found", "Not Found\n");
			return;
		}

		sendResponse(outBuffer, state, 200, "OK", body, "text/html");
		return;
	}

	// ---- POST /echo ----
	if (request.method == "POST" && request.path == "/echo")
	{
		sendResponse(outBuffer, state, 200, "OK", request.body);
		return;
	}

	// ---- unsupported method ----
	if (request.method != "GET" && request.method != "POST")
	{
		sendResponse(outBuffer, state, 405, "Method Not Allowed", "Method Not Allowed\n");
		return;
	}

	// ---- fallback ----
	sendResponse(outBuffer, state, 404, "Not Found", "Not Found\n");
}
