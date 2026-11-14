// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "catalog.h"
#include <cstdint>
struct CatalogCache {
 std::vector<Theme> entries;
 uint64_t fetched=0;
 bool more=false;
 bool fresh(uint64_t now) const { return !entries.empty() && fetched<=now && now-fetched<1800; }
};
bool read_catalog_cache(const std::string &path, int source, CatalogCache &cache);
bool write_catalog_cache(const std::string &path, int source, const CatalogCache &cache);
