#pragma once

#include <string>
#include "http/HttpRequest.hpp"
#include "http/HttpResponse.hpp"
#include "ServerConfig.hpp"

class HttpRouter
{
public:
	struct RouteResult
	{
		bool isCgi;
		std::string cgiInterpreter;
		std::string cgiScriptPath;
		HttpResponse response;

		RouteResult() : isCgi(false), cgiInterpreter(), cgiScriptPath(), response() {}
	};

	static HttpResponse route(const HttpRequest& req, const ServerConfig& cfg);
	static RouteResult route2(const HttpRequest& req, const ServerConfig& cfg);
};
