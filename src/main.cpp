#include "core/CoreServer.hpp"
#include "http/HttpHandler.hpp"
#include "ServerConfig.hpp"

#include <vector>

int main(int ac, char** av)
{
	std::string configPath = "config/default.conf";
	if (ac == 2)
		configPath = av[1];

	// 1️⃣ Создаём сервер
	CoreServer server(configPath);

	// 2️⃣ ВРЕМЕННО: создаём дефолтный ServerConfig вручную
	// (потом тут будет ConfigParser)
	std::vector<ServerConfig> configs;
	ServerConfig cfg;

	// пример error page
	cfg.errorPages[404] = "errors/404.html";

	// пример location /auto
	LocationConfig autoLoc;
	autoLoc.prefix = "/auto";
	autoLoc.autoindex = true;
	autoLoc.allowGet = true;
	autoLoc.allowHead = true;
	cfg.locations.push_back(autoLoc);

	configs.push_back(cfg);

	// 3️⃣ HttpHandler
	HttpHandler handler;
	handler.setServerConfigs(&configs);

	// 4️⃣ Связываем handler с core
	server.setHttpHandler(&handler);

	// 5️⃣ Запуск
	return server.run();
}
