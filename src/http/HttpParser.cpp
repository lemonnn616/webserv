#include "http/HttpParser.hpp"

#include <cctype>
#include <string>
#include <vector>
#include <map>

// ---------------- small helpers ----------------

static std::string toLower(const std::string& s)
{
	std::string r = s;
	for (std::size_t i = 0; i < r.size(); ++i)
		r[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(r[i])));
	return r;
}

static std::string trimLeftSpaces(const std::string& s)
{
	std::size_t i = 0;
	while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
		i++;
	return s.substr(i);
}

static bool isHex(char c)
{
	if (c >= '0' && c <= '9') return true;
	if (c >= 'a' && c <= 'f') return true;
	if (c >= 'A' && c <= 'F') return true;
	return false;
}

static int hexVal(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
	if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
	return -1;
}

// percent-decode (reject %00 and bad %XX)
static bool percentDecode(const std::string& in, std::string& out)
{
	out.clear();
	for (std::size_t i = 0; i < in.size(); ++i)
	{
		char c = in[i];
		if (c != '%')
		{
			out.push_back(c);
			continue;
		}
		if (i + 2 >= in.size())
			return false;
		if (!isHex(in[i + 1]) || !isHex(in[i + 2]))
			return false;

		int hi = hexVal(in[i + 1]);
		int lo = hexVal(in[i + 2]);
		if (hi < 0 || lo < 0)
			return false;

		unsigned char b = static_cast<unsigned char>((hi << 4) | lo);
		if (b == 0)
			return false; // reject NUL
		out.push_back(static_cast<char>(b));
		i += 2;
	}
	return true;
}

// normalize path: collapse //, handle . and .., keep leading '/'
// returns false if tries to go above root
static bool normalizePath(const std::string& decoded, std::string& normalized)
{
	if (decoded.empty() || decoded[0] != '/')
		return false;

	std::vector<std::string> parts;
	std::size_t i = 0;

	while (i < decoded.size())
	{
		while (i < decoded.size() && decoded[i] == '/')
			i++;

		std::size_t start = i;
		while (i < decoded.size() && decoded[i] != '/')
			i++;

		if (i <= start)
			continue;

		std::string seg = decoded.substr(start, i - start);

		if (seg == ".")
			continue;

		if (seg == "..")
		{
			if (parts.empty())
				return false;
			parts.pop_back();
			continue;
		}

		parts.push_back(seg);
	}

	normalized = "/";
	for (std::size_t k = 0; k < parts.size(); ++k)
	{
		normalized += parts[k];
		if (k + 1 < parts.size())
			normalized += "/";
	}
	return true;
}

static bool parseUnsignedSize(const std::string& s, std::size_t& out)
{
	if (s.empty())
		return false;

	std::size_t v = 0;
	for (std::size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] < '0' || s[i] > '9')
			return false;

		std::size_t digit = static_cast<std::size_t>(s[i] - '0');

		if (v > (static_cast<std::size_t>(-1) - digit) / 10)
			return false;

		v = v * 10 + digit;
	}
	out = v;
	return true;
}

static HttpParser::Result parseChunkedBody(
	const std::string& rest,
	std::size_t maxBody,
	std::string& outBody,
	std::size_t& consumed
)
{
	outBody.clear();
	consumed = 0;

	std::size_t p = 0;
	while (true)
	{
		std::size_t lineEnd = rest.find("\r\n", p);
		if (lineEnd == std::string::npos)
			return HttpParser::NEED_MORE;

		std::string sizeLine = rest.substr(p, lineEnd - p);

		std::size_t semi = sizeLine.find(';');
		if (semi != std::string::npos)
			sizeLine = sizeLine.substr(0, semi);

		sizeLine = trimLeftSpaces(sizeLine);
		while (!sizeLine.empty() && (sizeLine[sizeLine.size() - 1] == ' ' || sizeLine[sizeLine.size() - 1] == '\t'))
			sizeLine.erase(sizeLine.size() - 1, 1);

		if (sizeLine.empty())
			return HttpParser::BAD_REQUEST;

		std::size_t chunkSize = 0;
		for (std::size_t i = 0; i < sizeLine.size(); ++i)
		{
			if (!isHex(sizeLine[i]))
				return HttpParser::BAD_REQUEST;

			int hv = hexVal(sizeLine[i]);
			if (hv < 0)
				return HttpParser::BAD_REQUEST;

			if (chunkSize > (static_cast<std::size_t>(-1) >> 4))
				return HttpParser::BAD_REQUEST;

			chunkSize = (chunkSize << 4) + static_cast<std::size_t>(hv);
		}

		p = lineEnd + 2;

		if (rest.size() < p + chunkSize + 2)
			return HttpParser::NEED_MORE;

		if (chunkSize > 0)
		{
			if (outBody.size() + chunkSize > maxBody)
				return HttpParser::TOO_LARGE;

			outBody.append(rest, p, chunkSize);
		}
		p += chunkSize;

		if (rest.compare(p, 2, "\r\n") != 0)
			return HttpParser::BAD_REQUEST;
		p += 2;

		if (chunkSize == 0)
		{
			if (p == rest.size())
			{
				consumed = p;
				return HttpParser::OK;
			}
			if (rest.size() >= p + 2 && rest.compare(p, 2, "\r\n") == 0)
			{
				p += 2;
				consumed = p;
				return HttpParser::OK;
			}

			std::size_t trailersEnd = rest.find("\r\n\r\n", p);
			if (trailersEnd == std::string::npos)
				return HttpParser::NEED_MORE;

			consumed = trailersEnd + 4;
			return HttpParser::OK;
		}
	}
}

// ---------------- main parse ----------------

HttpParser::Result HttpParser::parse(std::string& inBuffer, HttpRequest& req, std::size_t maxBodySize)
{
	// find end of headers (CRLFCRLF)
	std::size_t headersEnd = inBuffer.find("\r\n\r\n");
	if (headersEnd == std::string::npos)
		return NEED_MORE;

	// IMPORTANT FIX:
	// request-line ends with the FIRST CRLF, which may be exactly at headersEnd when there are NO headers.
	std::size_t lineEnd = inBuffer.find("\r\n");
	if (lineEnd == std::string::npos || lineEnd > headersEnd)
		return BAD_REQUEST;

	std::string requestLine = inBuffer.substr(0, lineEnd);

	// headers part is between request-line CRLF and the CRLFCRLF
	std::size_t headersPartBegin = lineEnd + 2;
	std::string headersPart;
	if (headersPartBegin < headersEnd)
		headersPart = inBuffer.substr(headersPartBegin, headersEnd - headersPartBegin);
	else
		headersPart = "";

	// body starts after CRLFCRLF
	std::string rest = inBuffer.substr(headersEnd + 4);

	// parse request line
	std::size_t p1 = requestLine.find(' ');
	if (p1 == std::string::npos)
		return BAD_REQUEST;
	std::size_t p2 = requestLine.find(' ', p1 + 1);
	if (p2 == std::string::npos)
		return BAD_REQUEST;

	req.method  = requestLine.substr(0, p1);
	req.target  = requestLine.substr(p1 + 1, p2 - p1 - 1);
	req.version = requestLine.substr(p2 + 1);

	req.headers.clear();
	req.body.clear();
	req.path.clear();
	req.query.clear();

	bool isHttp11 = false;
	if (req.version == "HTTP/1.1")
		isHttp11 = true;
	else if (req.version == "HTTP/1.0")
		isHttp11 = false;
	else
		return BAD_REQUEST;

	// parse headers lines (headersPart contains lines separated by CRLF, WITHOUT the final empty line)
	std::size_t pos = 0;
	while (pos < headersPart.size())
	{
		std::size_t end = headersPart.find("\r\n", pos);
		std::string line;

		if (end == std::string::npos)
		{
			line = headersPart.substr(pos);
			pos = headersPart.size();
		}
		else
		{
			line = headersPart.substr(pos, end - pos);
			pos = end + 2;
		}

		if (line.empty())
			break;

		std::size_t colon = line.find(':');
		if (colon == std::string::npos)
			return BAD_REQUEST;

		std::string key = toLower(line.substr(0, colon));
		std::string value = trimLeftSpaces(line.substr(colon + 1));

		std::map<std::string, std::string>::iterator it = req.headers.find(key);
		if (it == req.headers.end())
			req.headers[key] = value;
		else
			it->second += "," + value;
	}

	// RFC: Host обязателен только для HTTP/1.1
	if (isHttp11)
	{
		if (req.headers.find("host") == req.headers.end())
			return BAD_REQUEST;
	}

	// absolute-form target -> strip scheme+host, keep path
	std::string target = req.target;
	if (target.size() >= 7 && target.compare(0, 7, "http://") == 0)
	{
		std::size_t slash = target.find('/', 7);
		target = (slash == std::string::npos) ? "/" : target.substr(slash);
	}
	else if (target.size() >= 8 && target.compare(0, 8, "https://") == 0)
	{
		std::size_t slash = target.find('/', 8);
		target = (slash == std::string::npos) ? "/" : target.substr(slash);
	}

	// split query
	std::size_t qpos = target.find('?');
	std::string rawPath;
	
	if (qpos == std::string::npos)
	{
		rawPath = target;
		req.query = "";
	}
	else
	{
		rawPath = target.substr(0, qpos);
		req.query = target.substr(qpos + 1);
	}
	if (rawPath.size() > 1 && rawPath[rawPath.size() - 1] == '/')
		req.hadTrailingSlash = true;

	// decode + normalize
	std::string decoded;
	if (!percentDecode(rawPath, decoded))
		return BAD_REQUEST;

	std::string normalized;
	if (!normalizePath(decoded, normalized))
		return BAD_REQUEST;

	req.path = normalized;
	if (req.hadTrailingSlash && req.path.size() > 1 && req.path[req.path.size() - 1] != '/')
		req.path += "/";

	// transfer-encoding
	bool isChunked = false;
	std::map<std::string, std::string>::const_iterator te = req.headers.find("transfer-encoding");
	if (te != req.headers.end())
	{
		std::string v = toLower(te->second);
		if (v.find("chunked") != std::string::npos)
			isChunked = true;
		else
			return BAD_REQUEST;
	}

	if (isChunked)
	{
		std::string body;
		std::size_t consumed = 0;

		Result r = parseChunkedBody(rest, maxBodySize, body, consumed);
		if (r != OK)
			return r;

		req.body = body;
		inBuffer.erase(0, headersEnd + 4 + consumed);
		return OK;
	}

	// content-length
	std::size_t contentLength = 0;
	std::map<std::string, std::string>::const_iterator cl = req.headers.find("content-length");
	if (cl != req.headers.end())
	{
		std::string v = trimLeftSpaces(cl->second);
		while (!v.empty() && (v[v.size() - 1] == ' ' || v[v.size() - 1] == '\t'))
			v.erase(v.size() - 1, 1);

		if (!parseUnsignedSize(v, contentLength))
			return BAD_REQUEST;
	}

	if (contentLength > maxBodySize)
		return TOO_LARGE;

	if (rest.size() < contentLength)
		return NEED_MORE;

	if (contentLength > 0)
		req.body = rest.substr(0, contentLength);

	inBuffer.erase(0, headersEnd + 4 + contentLength);
	return OK;
}
