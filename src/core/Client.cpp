#include "core/Client.hpp"

Client::Client()
	: fd(-1)
	, state(ConnectionState::CONNECTED)
	, inBuffer()
	, outBuffer()
	, lastActivity(std::chrono::steady_clock::now())
	, sessionId()
	, serverConfigIndex(0)
	, closeAfterWrite(false)
	, outOffset(0)
	, listenPort(0)
	, peerClosed(false)
{
}
