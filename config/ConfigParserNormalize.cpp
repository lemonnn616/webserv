#include "ConfigParser.hpp"

#include <algorithm>
#include <set>
#include <cctype>

static std::string toLowerStr(const std::string& s)
{
	std::string r = s;
	for (std::size_t i = 0; i < r.size(); ++i)
	{
		r[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(r[i])));
	}
	return r;
}

static std::string normalizePrefix(const std::string& in)
{
	std::string p = in;

	// must start with '/'
	if (p.empty() || p[0] != '/')
		return p;

	// remove trailing slashes except root "/"
	while (p.size() > 1 && p[p.size() - 1] == '/')
		p.erase(p.size() - 1);

	return p;
}

bool ConfigParser::normalizeAll(std::vector<ServerConfig>& out)
{
	for (std::size_t si = 0; si < out.size(); ++si)
	{
		ServerConfig& srv = out[si];

		if (srv.root.empty())
			srv.root = "www";
		if (srv.index.empty())
			srv.index = "index.html";

		// normalize server_name: lowercase + unique
		{
			std::set<std::string> seen;
			std::vector<std::string> names;
			names.reserve(srv.serverNames.size());

			for (std::size_t i = 0; i < srv.serverNames.size(); ++i)
			{
				std::string n = toLowerStr(srv.serverNames[i]);
				if (n.empty())
					continue;
				if (seen.insert(n).second)
					names.push_back(n);
			}
			srv.serverNames.swap(names);
		}

		bool hasRootLoc = false;

		// normalize locations
		for (std::size_t i = 0; i < srv.locations.size(); ++i)
		{
			LocationConfig& loc = srv.locations[i];

			loc.prefix = normalizePrefix(loc.prefix);

			if (loc.prefix.empty() || loc.prefix[0] != '/')
			{
				return setError(0, "Invalid location prefix: " + loc.prefix);
			}

			if (loc.prefix == "/")
				hasRootLoc = true;

			if (loc.index.empty())
				loc.index = srv.index;
		}

		// SAFER DEFAULT:
		// If user didn't define "/", we add it, but ONLY GET+HEAD.
		// POST/DELETE must be explicitly allowed in config via allowed_methods.
		if (!hasRootLoc)
		{
			LocationConfig loc;
			loc.prefix = "/";
			loc.root = "";
			loc.index = srv.index;
			loc.autoindex = false;

			loc.allowGet = true;
			loc.allowHead = true;
			loc.allowPost = false;
			loc.allowDelete = false;

			loc.hasReturn = false;
			loc.returnCode = 0;
			loc.returnUrl = "";

			srv.locations.push_back(loc);
		}

		// sort by longest prefix first (more specific location wins)
		std::stable_sort(
			srv.locations.begin(),
			srv.locations.end(),
			[](const LocationConfig& a, const LocationConfig& b)
			{
				if (a.prefix.size() != b.prefix.size())
					return a.prefix.size() > b.prefix.size();
				return a.prefix < b.prefix;
			}
		);
	}

	return true;
}
