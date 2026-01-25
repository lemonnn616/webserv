#pragma once

#include "http/IHttpHandler.hpp"
#include "ServerConfig.hpp"
#include <vector>

class HttpHandler : public IHttpHandler
{
public:
	HttpHandler();
	virtual ~HttpHandler();

	void setServerConfigs(const std::vector<ServerConfig>* cfgs);

	virtual void onDataReceived(
		int clientFd,
		std::string& inBuffer,
		std::string& outBuffer,
		ConnectionState& state,
		std::size_t serverConfigIndex,
		std::string& stateData
	);

private:
	const std::vector<ServerConfig>* _cfgs;
};
