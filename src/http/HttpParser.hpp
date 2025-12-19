#pragma once

#include <string>
#include "http/HttpRequest.hpp"

class HttpParser
{
public:
	static bool parse
	(
		std::string& inBuffer,
		HttpRequest& out,
		std::size_t maxBodySize
	);
};
