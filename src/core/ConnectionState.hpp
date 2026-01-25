#pragma once

enum class ConnectionState
{
	CONNECTED,
	READING,
	PARSED,
	WRITING,
	CGI_PENDING,
	CLOSING
};
