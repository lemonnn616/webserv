#include "http/ErrorPage.hpp"

std::string ErrorPage::defaultHtml(int status, const std::string& reason)
{
	return
		"<!doctype html><html><head><meta charset=\"utf-8\"></head><body>"
		"<h1>" + std::to_string(status) + " " + reason + "</h1>"
		"</body></html>";
}
