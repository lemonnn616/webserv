#include "ConfigParser.hpp"
#include "ServerConfig.hpp"
#include "core/Logger.hpp"

#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdlib>

ConfigParser::ConfigParser()
{
}

std::string ConfigParser::stripComment(const std::string& s)
{
	std::size_t p = s.find('#');
	if (p == std::string::npos)
		return s;
	return s.substr(0, p);
}

std::string ConfigParser::trim(const std::string& s)
{
	std::size_t i = 0;
	while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
		i++;

	std::size_t j = s.size();
	while (j > i && std::isspace(static_cast<unsigned char>(s[j - 1])))
		j--;

	return s.substr(i, j - i);
}

bool ConfigParser::parseFile(const std::string& path, ServerConfig& out)
{
	std::ifstream f(path.c_str());
	if (!f)
	{
		Logger::warn("Config not found, using defaults: " + path);
		return true;
	}

	std::string line;
	while (std::getline(f, line))
	{
		line = trim(stripComment(line));
		if (line.empty())
			continue;

		// поддержим вариант с ';'
		if (!line.empty() && line[line.size() - 1] == ';')
			line.erase(line.size() - 1);

		// игнорируем server { }
		if (line == "server" || line == "server{" || line == "{" || line == "}")
			continue;

		std::istringstream iss(line);
		std::string key;
		std::string value;

		iss >> key;
		if (key.empty())
			continue;

		iss >> value;
		if (value.empty())
			continue;

		if (key == "listen")
		{
			int p = std::atoi(value.c_str());
			if (p > 0 && p < 65536)
				out.listenPort = static_cast<unsigned short>(p);
		}
		else if (key == "root")
		{
			out.root = value;
		}
		else if (key == "index")
		{
			out.index = value;
		}
		else if (key == "upload_dir")
		{
			out.uploadDir = value;
		}
		else if (key == "client_max_body_size")
		{
			long n = std::atol(value.c_str());
			if (n > 0)
				out.clientMaxBodySize = static_cast<std::size_t>(n);
		}
	}

	return true;
}
