#include "utils/FileUtils.hpp"

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>

namespace FileUtils
{
	static bool statPath(const std::string& path, struct stat& st)
	{
		return (::stat(path.c_str(), &st) == 0);
	}

	bool exists(const std::string& path)
	{
		struct stat st;
		return statPath(path, st);
	}

	bool isDirectory(const std::string& path)
	{
		struct stat st;
		if (!statPath(path, st))
			return false;
		return S_ISDIR(st.st_mode);
	}

	bool readFile(const std::string& path, std::string& out)
	{
		out.clear();

		struct stat st;
		if (!statPath(path, st))
			return false;
		if (!S_ISREG(st.st_mode))
			return false;

		int fd = ::open(path.c_str(), O_RDONLY);
		if (fd < 0)
			return false;

		char buf[8192];

		while (true)
		{
			ssize_t n = ::read(fd, buf, sizeof(buf));
			if (n > 0)
			{
				out.append(buf, static_cast<std::size_t>(n));
				continue;
			}
			if (n == 0)
				break;
			if (errno == EINTR)
				continue;
			::close(fd);
			out.clear();
			return false;
		}

		::close(fd);
		return true;
	}

	bool writeFile(const std::string& path, const std::string& data)
	{
		int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd < 0)
			return false;

		std::size_t off = 0;
		while (off < data.size())
		{
			ssize_t n = ::write(fd, data.data() + off, data.size() - off);
			if (n > 0)
			{
				off += static_cast<std::size_t>(n);
				continue;
			}
			if (n < 0 && errno == EINTR)
				continue;

			::close(fd);
			return false;
		}

		::close(fd);
		return true;
	}

	std::string join(const std::string& a, const std::string& b)
	{
		if (a.empty())
			return b;
		if (b.empty())
			return a;

		if (a[a.size() - 1] == '/')
		{
			if (b[0] == '/')
				return a + b.substr(1);
			return a + b;
		}

		if (b[0] == '/')
			return a + b;

		return a + "/" + b;
	}
}
