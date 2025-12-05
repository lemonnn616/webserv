#include "core/Client.hpp"

Client::Client()
	:fd(-1)
	,state(ConnectionState::CONNECTED)
	,inBuffer()
	,writeQueue()
	,writeOffset(0)
	,lastActivity(std::chrono::steady_clock::now())
	,sessionId()
	,serverConfigIndex(0)
{
}
