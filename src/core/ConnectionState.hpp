#pragma once

enum class ConnectionState
{
	CONNECTED,
	READING,
	PARSED,
	WRITING,
	CLOSING
};
