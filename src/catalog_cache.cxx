// SPDX-License-Identifier: GPL-2.0-or-later
#include "catalog_cache.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
static bool valid(const Theme &t) {
 return !t.title.empty() && t.title.size()<=150 && t.title.find_first_of("\r\n") == std::string::npos &&
 t.page.size()<=4096 && allowed_url(t.page) && t.download.size()<=4096 &&
 (t.download.empty() || allowed_url(t.download));
}
bool read_catalog_cache(const std::string &path,int source,CatalogCache &cache) {
 FILE *f=fopen(path.c_str(),"rb");if(!f)return false;
 char line[4200];int saved=-1,more=0;unsigned count=0;unsigned long long stamp=0;
 bool ok=fgets(line,sizeof line,f) && sscanf(line,"TS-CACHE-2 %d %llu %u %d",&saved,&stamp,&count,&more)==4 && saved==source && count>0 && count<=128 && (more==0 || more==1);
 CatalogCache tmp;tmp.fetched=stamp;tmp.more=more!=0;
 auto read=[&](std::string &out) {
  if(!fgets(line,sizeof line,f))return false;
  size_t n=strlen(line);if(!n || line[n-1]!='\n')return false;
  out.assign(line,n-1);return true;
 };
 for(unsigned i=0;ok && i<count;++i) {
  Theme t;ok=read(t.title) && read(t.page) && read(t.download) && valid(t);
  if(ok)tmp.entries.push_back(t);
 }
 if(ok && fgetc(f)!=EOF)ok=false;
 fclose(f);if(ok)cache=tmp;return ok;
}
bool write_catalog_cache(const std::string &path,int source,const CatalogCache &cache) {
 if(cache.entries.empty() || cache.entries.size()>128)return false;
 for(const auto &t:cache.entries)if(!valid(t))return false;
 std::string temp=path+".tmp";FILE *f=fopen(temp.c_str(),"wb");if(!f)return false;
 bool ok=fprintf(f,"TS-CACHE-2 %d %llu %u %d\n",source,(unsigned long long)cache.fetched,(unsigned)cache.entries.size(),cache.more?1:0)>0;
 for(const auto &t:cache.entries)if(fprintf(f,"%s\n%s\n%s\n",t.title.c_str(),t.page.c_str(),t.download.c_str())<0)ok=false;
 if(fclose(f)!=0)ok=false;
 if(ok && rename(temp.c_str(),path.c_str())==0)return true;
 remove(temp.c_str());return false;
}
