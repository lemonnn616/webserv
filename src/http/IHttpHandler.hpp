#pragma once

#include <string>
#include "core/ConnectionState.hpp"

class IHttpHandler
{
public:
	
	virtual ~IHttpHandler()
	{
	}

	virtual void onDataReceived
	(
		int clientFd,
		std::string& inBuffer,
		std::string& outBuffer,
		ConnectionState& state,
		std::size_t serverConfigIndex,
		std::string& sessionId
	)=0;
};
