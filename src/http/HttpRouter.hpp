#pragma once

#include "http/HttpRequest.hpp"
#include "http/HttpResponse.hpp"
#include "ServerConfig.hpp"

class HttpRouter
{
public:
	static HttpResponse route(const HttpRequest& req, const ServerConfig& cfg);
};
