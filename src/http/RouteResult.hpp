#pragma once

#include <string>
#include "http/HttpResponse.hpp"

struct RouteResult
{
	bool isCgi;

	// CGI data (valid if isCgi==true)
	std::string interpreter;
	std::string scriptPath;

	// Normal HTTP response (valid if isCgi==false)
	HttpResponse res;

	RouteResult() : isCgi(false), interpreter(), scriptPath(), res() {}
};
