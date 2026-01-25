#include "http/CgiResponseParser.hpp"

#include <cctype>
#include <cstdlib>

static std::string toLowerStr(const std::string& s)
{
	std::string r = s;
	for (std::size_t i = 0; i < r.size(); ++i)
		r[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(r[i])));
	return r;
}

bool CgiResponseParser::parse(const std::string& out, HttpResponse& res)
{
	std::size_t sep = out.find("\r\n\r\n");
	if (sep == std::string::npos)
	{
		sep = out.find("\n\n");
		if (sep == std::string::npos)
			return false;
	}

	std::string head = out.substr(0, sep);
	std::string body;

	if (out.size() > sep)
	{
		if (out.compare(sep, 4, "\r\n\r\n") == 0)
			body = out.substr(sep + 4);
		else
			body = out.substr(sep + 2);
	}

	res.status = 200;
	res.reason = "OK";
	res.body = body;

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

		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);

		if (line.empty())
			continue;

		std::size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;

		std::string key = line.substr(0, colon);
		std::string val = line.substr(colon + 1);

		while (!val.empty() && (val[0] == ' ' || val[0] == '\t'))
			val.erase(0, 1);

		std::string keyLower = toLowerStr(key);

		if (keyLower == "status")
		{
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
			res.headers[key] = val;
		}
	}

	if (res.headers.find("Content-Length") == res.headers.end())
		res.headers["Content-Length"] = std::to_string(res.body.size());

	if (res.headers.find("Content-Type") == res.headers.end())
		res.headers["Content-Type"] = "text/plain";

	return true;
}
