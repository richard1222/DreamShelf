// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <vector>
struct Theme { std::string title, page, download; };
struct Source { const char *name, *url; };
extern const Source sources[3];
std::vector<Theme> parse_catalog(int source, const std::string &html);
std::string parse_download(const std::string &html);
bool allowed_url(const std::string &url);
std::string filename_for(const Theme &theme);
bool p3t_header(const unsigned char *data, size_t length);
std::string catalog_page_url(int source, unsigned page);
bool catalog_has_next(int source, const std::string &html);
