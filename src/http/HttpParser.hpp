#pragma once

#include <string>
#include "http/HttpRequest.hpp"
#include <cstddef>

class HttpParser
{
public:
	enum Result
	{
		NEED_MORE = 0,
		OK = 1,
		BAD_REQUEST = -1,
		TOO_LARGE = -2
	};

	static Result parse
	(
		std::string& inBuffer,
		HttpRequest& out,
		std::size_t maxBodySize
	);
};
