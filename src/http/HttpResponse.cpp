#include "http/HttpResponse.hpp"

static std::string safeReasonPhrase(const std::string& reason)
{
	if (reason.empty())
		return "OK";

	std::string out;
	out.reserve(reason.size());

	for (std::size_t i = 0; i < reason.size(); ++i)
	{
		unsigned char c = static_cast<unsigned char>(reason[i]);
		if (c == '\r' || c == '\n')
			break;
		if (c < 32 || c > 126)
			continue;
		out.push_back(static_cast<char>(c));
	}

	if (out.empty())
		return "OK";

	return out;
}

std::string HttpResponse::serialize() const
{
	std::string result;
	const std::string reasonSafe = safeReasonPhrase(reason);

	
	if (version.empty())
		result += "HTTP/1.1";
	else
		result += version;

	result += " ";
	result += std::to_string(status);
	result += " ";
	result += reasonSafe;
	result += "\r\n";

	
	for (std::map<std::string, std::string>::const_iterator it = headers.begin();
		 it != headers.end(); ++it)
	{
		result += it->first;
		result += ": ";
		result += it->second;
		result += "\r\n";
	}

	
	result += "\r\n";
	result += body;

	return result;
}
