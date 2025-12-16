#include "http/HttpHandler.hpp"
#include "http/HttpRequest.hpp"
#include "http/HttpResponse.hpp"

#include <fstream>

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

static void sendSimpleResponse(
	std::string& outBuffer,
	ConnectionState& state,
	int status,
	const std::string& reason,
	const std::string& body
)
{
	HttpResponse response;
	response.status = status;
	response.reason = reason;
	response.body = body;
	response.headers["Content-Length"] = std::to_string(body.size());
	response.headers["Connection"] = "close";

	outBuffer = response.serialize();
	state = ConnectionState::WRITING;
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
	// Есть ли полный HTTP-запрос
	size_t headersEnd = inBuffer.find("\r\n\r\n");
	if (headersEnd == std::string::npos)
		return;

	// Забираем ОДИН запрос
	std::string requestBlock = inBuffer.substr(0, headersEnd);
	inBuffer.erase(0, headersEnd + 4);

	// Делим на request-line и headers
	size_t lineEnd = requestBlock.find("\r\n");
	if (lineEnd == std::string::npos)
	{
		sendSimpleResponse(outBuffer, state, 400, "Bad Request", "Bad Request\n");
		return;
	}

	std::string requestLine = requestBlock.substr(0, lineEnd);
	std::string headersPart = requestBlock.substr(lineEnd + 2);

	HttpRequest request;

	// -------- request-line --------
	size_t pos1 = requestLine.find(' ');
	size_t pos2 = requestLine.find(' ', pos1 + 1);

	if (pos1 == std::string::npos || pos2 == std::string::npos)
	{
		sendSimpleResponse(outBuffer, state, 400, "Bad Request", "Bad Request\n");
		return;
	}

	request.method = requestLine.substr(0, pos1);
	request.path = requestLine.substr(pos1 + 1, pos2 - pos1 - 1);
	request.version = requestLine.substr(pos2 + 1);

	// -------- headers --------
	size_t start = 0;
	while (start < headersPart.size())
	{
		size_t end = headersPart.find("\r\n", start);
		if (end == std::string::npos)
			break;

		std::string line = headersPart.substr(start, end - start);
		start = end + 2;

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

	// ================== routing ==================

	// Поддерживаем только GET и HEAD
	if (request.method != "GET" && request.method != "HEAD")
	{
		HttpResponse response;
		response.status = 405;
		response.reason = "Method Not Allowed";
		response.body = "Method Not Allowed\n";
		response.headers["Content-Length"] = "19";
		response.headers["Connection"] = "close";
		response.headers["Allow"] = "GET, HEAD";

		outBuffer = response.serialize();
		state = ConnectionState::WRITING;
		return;
	}

	// Защита от ../
	if (request.path.find("..") != std::string::npos)
	{
		sendSimpleResponse(outBuffer, state, 403, "Forbidden", "Forbidden\n");
		return;
	}

	// default index
	std::string path = request.path;
	if (path == "/")
		path = "/index.html";

	std::string fullPath = "www" + path;

	// ================== static file ==================

	std::string body;
	if (!readFile(fullPath, body))
	{
		sendSimpleResponse(outBuffer, state, 404, "Not Found", "Not Found\n");
		return;
	}

	HttpResponse response;
	response.status = 200;
	response.reason = "OK";
	response.headers["Content-Length"] = std::to_string(body.size());
	response.headers["Content-Type"] = getContentType(fullPath);
	response.headers["Connection"] = "close";

	if (request.method == "GET")
		response.body = body;
	else
		response.body = "";

	outBuffer = response.serialize();
	state = ConnectionState::WRITING;
}
