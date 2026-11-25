// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <vector>
struct Theme { std::string title, page, download; };
struct Source { const char *name, *url; };
extern const Source sources[4];
std::vector<Theme> parse_catalog(int source, const std::string &html);
std::string parse_download(const std::string &html, const std::string &base_url = "");
bool allowed_url(const std::string &url);
std::string resolve_url(const std::string &base, const std::string &ref);
std::string themezer_download_url(const std::string &page_url);
std::string filename_for(const Theme &theme);
bool p3t_header(const unsigned char *data, size_t length);
std::string catalog_page_url(int source, unsigned page);
bool catalog_has_next(int source, const std::string &html);
// Source 3 (Saber Pack): bundled themes shipped in the pkg. Builds the
// entry list from the static manifest, keeping only files that exist
// under pack_dir and pass the P3TF header check. Entry page/download
// fields carry "pack://<bundled filename>".
std::vector<Theme> pack_catalog(const std::string &pack_dir);
// Update detection: extract the "version" field from the update.json
// manifest body, and compare dotted versions numerically (a leading
// 'v' is tolerated; missing components count as 0).
std::string parse_update_version(const std::string &json);
bool update_is_newer(const std::string &latest, const std::string &current);
