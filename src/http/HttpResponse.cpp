#include "http/HttpResponse.hpp"

std::string HttpResponse::serialize() const
{
	std::string result;

	// status line
	if (version.empty())
		result += "HTTP/1.1";
	else
		result += version;

	result += " ";
	result += std::to_string(status);
	result += " ";
	result += reason;
	result += "\r\n";

	// headers
	for (std::map<std::string, std::string>::const_iterator it = headers.begin();
		 it != headers.end(); ++it)
	{
		result += it->first;
		result += ": ";
		result += it->second;
		result += "\r\n";
	}

	// end headers + body
	result += "\r\n";
	result += body;

	return result;
}
