#pragma once

#include <string>
#include "http/HttpResponse.hpp"
#include "ServerConfig.hpp"

class HttpError
{
public:
	static void fill(HttpResponse& res, const ServerConfig& cfg, int code, const std::string& reason);
};
