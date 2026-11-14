#include "catalog_cache.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
int main(int argc,char **argv) {
 assert(argc==2);std::string path=argv[1];
 CatalogCache original;original.fetched=1000;original.more=true;
 original.entries={{"A & B","http://www.ps3-themes.com/a.html",""},{"Backup","https://whatwhatboy.com/console/ps3/themes.html","https://raw.githubusercontent.com/example/themes/main/a.p3t"}};
 assert(original.fresh(1000));assert(original.fresh(2799));
 assert(!original.fresh(2800));assert(!original.fresh(999));
 assert(write_catalog_cache(path,1,original));
 CatalogCache restored;assert(read_catalog_cache(path,1,restored));
 assert(restored.entries.size()==2 && restored.fetched==1000 && restored.more);
 assert(restored.entries[0].title=="A & B" && restored.entries[0].download.empty());
 assert(restored.entries[1].download==original.entries[1].download);
 assert(!read_catalog_cache(path,0,restored));assert(restored.entries.size()==2);
 {std::ofstream f(path);f<<"TS-CACHE-2 1 1000 129 1\n";}
 assert(!read_catalog_cache(path,1,restored));
 {std::ofstream f(path);f<<"TS-CACHE-2 1 1000 1 1\nTitle\nfile:///dev_flash/file\n\n";}
 assert(!read_catalog_cache(path,1,restored));
 {std::ofstream f(path);f<<"TS-CACHE-2 1 1000 1 1\nTitle\nhttps://themezer.net/abc";}
 assert(!read_catalog_cache(path,1,restored));
 original.entries[0].title="bad\nrecord";assert(!write_catalog_cache(path,1,original));
 std::remove(path.c_str());
 std::cout<<"Cache round-trip, expiry, future clock, source mismatch, bounds, unsafe URL and truncation checks passed\n";
}
