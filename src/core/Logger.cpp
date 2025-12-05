#include "core/Logger.hpp"
#include <iostream>

void Logger::info(const std::string& msg)
{
	std::clog<<"[INFO] "<<msg<<std::endl;
}

void Logger::warn(const std::string& msg)
{
	std::clog<<"[WARN] "<<msg<<std::endl;
}

void Logger::error(const std::string& msg)
{
	std::cerr<<"[ERROR] "<<msg<<std::endl;
}

void Logger::debug(const std::string& msg)
{
	std::clog<<"[DEBUG] "<<msg<<std::endl;
}
