#pragma once
#include <string>
#include "ServerConfig.hpp"
#include "core/ConnectionState.hpp"

class ErrorPage
{
public:
	static std::string defaultHtml(int status, const std::string& reason);
};
