#include "http/HttpResponse.hpp"
#include <sstream>

std::string HttpResponse::serialize() const
{
	std::string result;
	result += "HTTP/1.1 ";
	result += std::to_string(status);
	result += " ";
	result += reason;
	result += "\r\n";

	for(std::map<std::string
		, std::string>::const_iterator it = headers.begin();
		it != headers.end();
		it++
		)
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