#pragma once

#include <string>

namespace FileUtils
{
	bool readFile(const std::string& path, std::string& out);
	bool writeFile(const std::string& path, const std::string& data);
	bool exists(const std::string& path);
	bool isDirectory(const std::string& path);
	std::string join(const std::string& a, const std::string& b);
}
