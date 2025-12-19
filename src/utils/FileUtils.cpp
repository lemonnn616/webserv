#include "utils/FileUtils.hpp"

#include <fstream>
#include <sys/stat.h>
#include <dirent.h>

namespace FileUtils
{
	bool readFile(const std::string& path, std::string& out)
	{
		std::ifstream f(path.c_str(), std::ios::in | std::ios::binary);
		if (!f)
			return false;

		out.assign(
			(std::istreambuf_iterator<char>(f)),
			std::istreambuf_iterator<char>()
		);
		return true;
	}

	bool writeFile(const std::string& path, const std::string& data)
	{
		std::ofstream f(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
		if (!f)
			return false;

		f.write(data.data(), static_cast<std::streamsize>(data.size()));
		return f.good();
	}

	bool exists(const std::string& path)
	{
		struct stat st;
		return (stat(path.c_str(), &st) == 0);
	}

	bool isDirectory(const std::string& path)
	{
		DIR* d = opendir(path.c_str());
		if (d)
		{
			closedir(d);
			return true;
		}
		return false;
	}

	std::string join(const std::string& a, const std::string& b)
	{
		if (a.empty())
			return b;
		if (a[a.size() - 1] == '/')
			return a + b;
		return a + "/" + b;
	}
}
