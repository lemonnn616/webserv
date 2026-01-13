#include "http/HttpParser.hpp"
#include <cstdlib>

static std::string trimLeft(const std::string& s)
{
	if (!s.empty() && s[0] == ' ')
		return s.substr(1);
	return s;
}

HttpParser::Result HttpParser::parse(
	std::string& inBuffer,
	HttpRequest& req,
	std::size_t maxBodySize
)
{
	std::size_t headersEnd = inBuffer.find("\r\n\r\n");
	if (headersEnd == std::string::npos)
		return NEED_MORE;

	std::string headersBlock = inBuffer.substr(0, headersEnd);
	std::string rest = inBuffer.substr(headersEnd + 4);

	std::size_t lineEnd = headersBlock.find("\r\n");
	if (lineEnd == std::string::npos)
		return BAD_REQUEST;

	std::string requestLine = headersBlock.substr(0, lineEnd);
	std::string headersPart = headersBlock.substr(lineEnd + 2);

	std::size_t p1 = requestLine.find(' ');
	std::size_t p2 = requestLine.find(' ', p1 + 1);
	if (p1 == std::string::npos || p2 == std::string::npos)
		return BAD_REQUEST;

	req.method = requestLine.substr(0, p1);
	req.path = requestLine.substr(p1 + 1, p2 - p1 - 1);
	req.version = requestLine.substr(p2 + 1);

	req.headers.clear();
	req.body.clear();

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
		std::string value = trimLeft(line.substr(colon + 1));
		req.headers[key] = value;
	}

	std::size_t contentLength = 0;
	if (req.headers.count("Content-Length"))
		contentLength = static_cast<std::size_t>(
			std::atoi(req.headers["Content-Length"].c_str())
		);

	if (contentLength > maxBodySize)
		return TOO_LARGE;

	if (rest.size() < contentLength)
		return NEED_MORE;

	req.body = rest.substr(0, contentLength);
	inBuffer.erase(0, headersEnd + 4 + contentLength);

	return OK;
}
