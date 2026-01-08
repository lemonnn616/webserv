#include "ConfigParser.hpp"

#include <fstream>
#include <sstream>
#include <cctype>

std::string ConfigParser::readFile(const std::string& path)
{
	std::ifstream f(path.c_str(),std::ios::in|std::ios::binary);
	if(!f)
	{
		return "";
	}

	std::ostringstream ss;
	ss<<f.rdbuf();
	return ss.str();
}

std::string ConfigParser::stripComments(const std::string& s)
{
	std::string out;
	out.reserve(s.size());

	std::size_t i=0;
	while(i<s.size())
	{
		if(s[i]=='#')
		{
			while(i<s.size()&& s[i]!='\n')
			{
				++i;
			}
			continue;
		}
		out.push_back(s[i]);
		++i;
	}
	return out;
}

std::vector<ConfigParser::Token> ConfigParser::tokenize(const std::string& s)
{
	std::vector<Token> t;
	std::string cur;
	std::size_t line=1;
	std::size_t curLine=1;

	for(std::size_t i=0;i<s.size();++i)
	{
		char c=s[i];

		if(c=='\n')
		{
			if(!cur.empty())
			{
				Token tok;
				tok.text=cur;
				tok.line=curLine;
				t.push_back(tok);
				cur.clear();
			}
			++line;
			continue;
		}

		if(c=='{'||c=='}'||c==';')
		{
			if(!cur.empty())
			{
				Token tok;
				tok.text=cur;
				tok.line=curLine;
				t.push_back(tok);
				cur.clear();
			}
			Token one;
			one.text=std::string(1,c);
			one.line=line;
			t.push_back(one);
			continue;
		}

		if(std::isspace(static_cast<unsigned char>(c)))
		{
			if(!cur.empty())
			{
				Token tok;
				tok.text=cur;
				tok.line=curLine;
				t.push_back(tok);
				cur.clear();
			}
			continue;
		}

		if(cur.empty())
		{
			curLine=line;
		}
		cur.push_back(c);
	}

	if(!cur.empty())
	{
		Token tok;
		tok.text=cur;
		tok.line=curLine;
		t.push_back(tok);
	}

	return t;
}
