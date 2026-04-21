#pragma once

#include <string>
#include "http/HttpResponse.hpp"

struct RouteResult
{
	bool isCgi;

	
	std::string interpreter;
	std::string scriptPath;

	
	HttpResponse res;

	RouteResult() : isCgi(false), interpreter(), scriptPath(), res() {}
};
