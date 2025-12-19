#pragma once
#include <string>

class AutoIndex
{
public:
	static std::string generate(const std::string& urlPath, const std::string& fsDir);
};
