#pragma once

#include <string>
#include "http/HttpResponse.hpp"

class CgiResponseParser
{
public:
	static bool parse(const std::string& out, HttpResponse& res);
};
