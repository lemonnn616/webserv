#include <iostream>
#include "core/CoreServer.hpp"

int	main(int argc, char** argv)
{
	const char* configPath = (argc > 1) ? argv[1] : "config/default.conf";

	try
	{
		CoreServer server(configPath);
		return server.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Fatal: " << e.what() << std::endl;
		return 1;
	}
}
