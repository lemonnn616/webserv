#include <iostream>

#include "core/CoreServer.hpp"
#include "http/HttpHandler.hpp"

int main(int argc, char** argv)
{
	const char* configPath;

	if(argc>1)
		configPath=argv[1];
	else
		configPath="config/default.conf";

	try
	{
		CoreServer server(configPath);

		HttpHandler httpHandler;
		httpHandler.setServerConfigs(&server.getServerConfigs());
		server.setHttpHandler(&httpHandler);

		return server.run();
	}
	catch(const std::exception& e)
	{
		std::cerr<<"Fatal: "<<e.what()<<std::endl;
		return 1;
	}
}
