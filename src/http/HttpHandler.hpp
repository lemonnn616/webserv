#pragma once

#include "http/IHttpHandler.hpp"

class HttpHandler : public IHttpHandler
{
public:
	HttpHandler();
	virtual ~HttpHandler();

	virtual void onDataReceived(
		int clientFd,
		std::string& inBuffer, // все непрочитанные байты от клиента
		std::string& outBuffer, // сюда мы кладём http ответ 
		ConnectionState& state, // говорим ядру что делать дальше
		std::size_t serverConfigIndex,
		std::string& sessionId
	);
};
