#include "http/AutoIndex.hpp"
#include <dirent.h>

std::string AutoIndex::generate(const std::string& url, const std::string& dir)
{
	DIR* d = opendir(dir.c_str());
	if (!d)
		return "";

	std::string html =
		"<!doctype html><html><head><meta charset=\"utf-8\"></head><body>"
		"<h1>Index of " + url + "</h1><ul>";

	struct dirent* e;
	while ((e = readdir(d)) != 0)
	{
		std::string name(e->d_name);
		if (name == "." || name == "..")
			continue;

		html += "<li><a href=\"" + url;
		if (url[url.size() - 1] != '/')
			html += "/";
		html += name + "\">" + name + "</a></li>";
	}

	html += "</ul></body></html>";
	closedir(d);
	return html;
}
