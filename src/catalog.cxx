// SPDX-License-Identifier: GPL-2.0-or-later
#include "catalog.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>
const Source sources[3] = {
 {"PS3-Themes", "http://www.ps3-themes.com/"},
 {"Themezer", "https://themezer.net/ps3/themes"},
 {"Curated backup", "https://whatwhatboy.com/console/ps3/themes.html"}
};
static std::string attribute(const std::string &tag, const std::string &key) {
 size_t i=tag.find_first_of(" \t\r\n"); if(i==std::string::npos)return {};
 while(i<tag.size()) {
  while(i<tag.size() && (isspace((unsigned char)tag[i]) || tag[i]=='/')) ++i;
  if(i>=tag.size() || tag[i]=='>')break;
  size_t begin=i;
  while(i<tag.size() && !isspace((unsigned char)tag[i]) && tag[i]!='=' && tag[i]!='>') ++i;
  std::string name=tag.substr(begin,i-begin);
  while(i<tag.size() && isspace((unsigned char)tag[i])) ++i;
  if(i>=tag.size() || tag[i]!='=') { continue; }
  ++i; while(i<tag.size() && isspace((unsigned char)tag[i])) ++i;
  char quote=(i<tag.size() && (tag[i]=='\'' || tag[i]=='"'))?tag[i++]:0;
  begin=i;
  while(i<tag.size() && (quote?tag[i]!=quote:(!isspace((unsigned char)tag[i]) && tag[i]!='>'))) ++i;
  if(name==key) return tag.substr(begin,i-begin);
  if(quote && i<tag.size()) ++i;
 }
 return {};
}
static std::string clean(std::string s) {
 std::string o; bool tag=false,space=true;
 for(size_t i=0;i<s.size() && o.size()<150;++i) {
  unsigned char c=s[i]; if(c=='<') {tag=true;continue;} if(c=='>') {tag=false;continue;} if(tag) continue;
  if(c=='&') {
   size_t e=s.find(';',i+1);
   if(e!=std::string::npos && e-i<12) {
    std::string entity=s.substr(i+1,e-i-1); c='?';
    if(entity=="amp") c='&'; else if(entity=="quot") c='"'; else if(entity=="apos" || entity=="#39") c='\'';
    else if(entity=="lt") c='<'; else if(entity=="gt") c='>'; else if(entity=="nbsp") c=' ';
    else if(entity=="#8211" || entity=="#8212") c='-';
    i=e;
   }
  }
  if(c>=128) continue;
  if(isspace(c)) {if(!space) o+=' ';space=true;} else {o+=(char)c;space=false;}
 }
 while(!o.empty() && o.back()==' ')o.pop_back();
 return o;
}
bool allowed_url(const std::string &url) {
 for(unsigned char c:url)if(c<=32 || c==127 || c=='\\')return false;
 size_t p=url.find("://"); if(p==std::string::npos)return false;
 if(url.substr(0,p)!="http" && url.substr(0,p)!="https")return false;
 size_t end=url.find_first_of("/?#",p+3);std::string host=url.substr(p+3,end==std::string::npos?end:end-p-3);
 if(host.find_first_of("@:\\")!=std::string::npos)return false;
 const char *hosts[]={"www.ps3-themes.com","ps3-themes.com","download.ps3-themes.com","themezer.net","api.themezer.net","ps3.themezer.net","raw.githubusercontent.com","whatwhatboy.com","themezer.369b61bc4ba6857084011528601a3a1d.r2.cloudflarestorage.com"};
 for(auto h:hosts)if(host==h)return true;
 return false;
}
std::string parse_download(const std::string &html) {
 size_t i=0;
 while((i=html.find("<a",i))!=std::string::npos) {
  size_t end=html.find('>',i);if(end==std::string::npos)break;
  auto url=attribute(html.substr(i,end-i+1),"href");i=end+1;
  if(allowed_url(url) && (url.find(".p3t")!=std::string::npos || url.find("/download")!=std::string::npos))return url;
 }
 return {};
}
std::vector<Theme> parse_catalog(int source, const std::string &html) {
 std::vector<Theme> out;std::set<std::string> seen;
 if(source<0 || source>2 || html.size()>4*1024*1024)return out;
 const char *marker=source==0?"entry-title":source==1?"class=\"card-link\"":"class=\"ps3-theme-card\"";
 size_t i=0;
 while((i=html.find(marker,i))!=std::string::npos && out.size()<128) {
  size_t begin=html.rfind('<',i),end=html.find(source==0?"</h2>":source==1?"</a>":"</article>",i);
  ++i;if(begin==std::string::npos || end==std::string::npos || end-begin>100000)continue;
  std::string block=html.substr(begin,end-begin),title,url;
  if(source==0) {
   size_t a=block.find("<a"),gt=block.find('>',a),close=block.find("</a>",gt);
   if(a==std::string::npos || gt==std::string::npos || close==std::string::npos)continue;
   url=attribute(block.substr(a,gt-a+1),"href");title=clean(block.substr(gt+1,close-gt-1));
  } else if(source==1) {
   size_t gt=block.find('>');url=attribute(block.substr(0,gt+1),"href");
   if(url.rfind("/ps3/themes/",0)!=0)continue;
   url="https://themezer.net"+url;
   size_t t=block.find("card-title"),start=block.find('>',t),close=block.find("</div>",start);
   if(t==std::string::npos || start==std::string::npos || close==std::string::npos)continue;
   title=clean(block.substr(start+1,close-start-1));
  } else {
   size_t t=block.find("<h3"),start=block.find('>',t),close=block.find("</h3>",start);
   if(t==std::string::npos || start==std::string::npos || close==std::string::npos)continue;
   title=clean(block.substr(start+1,close-start-1));url=parse_download(block);
  }
  if(title.empty() || !allowed_url(url) || !seen.insert(url).second)continue;
  out.push_back({title,url,source==2?url:""});
 }
 return out;
}
std::string filename_for(const Theme &t) {
 std::string name;for(unsigned char c:t.title)if(name.size()<64)name+=(isalnum(c)||c=='-'||c=='_')?(char)c:'_';
 uint32_t hash=2166136261u;for(unsigned char c:t.page)hash=(hash^c)*16777619u;
 const char hex[]="0123456789abcdef";name+='-';for(int i=7;i>=0;--i)name+=hex[(hash>>(i*4))&15];
 return name+".p3t";
}
bool p3t_header(const unsigned char *p,size_t n) {return n>=64 && p[0]=='P' && p[1]=='3' && p[2]=='T' && p[3]=='F';}
std::string catalog_page_url(int source,unsigned page) {
 if(source<0 || source>2 || page==0)return {};
 if(page==1)return sources[source].url;
 if(source==0)return "http://www.ps3-themes.com/page/"+std::to_string(page);
 if(source==1)return "https://themezer.net/ps3/themes?page="+std::to_string(page);
 return {};
}
bool catalog_has_next(int source,const std::string &html) {
 if(source==2)return false;
 if(source==0) {
  size_t i=0;
  while((i=html.find('<',i))!=std::string::npos) {
   size_t end=html.find('>',i);if(end==std::string::npos)break;
   auto tag=html.substr(i,end-i+1);i=end+1;
   if((tag.rfind("<link ",0)==0 || tag.rfind("<a ",0)==0) && attribute(tag,"rel")=="next") {
    auto u=attribute(tag,"href");
    if(u.rfind("http://www.ps3-themes.com/page/",0)==0 || u.rfind("https://www.ps3-themes.com/page/",0)==0)return true;
   }
  }
 } else if(source==1) {
  size_t i=html.find("aria-label=\"Next Page\"");if(i==std::string::npos)return false;
  size_t begin=html.rfind("<button",i),end=html.find('>',i);
  if(begin==std::string::npos || end==std::string::npos)return false;
  auto tag=html.substr(begin,end-begin+1);
  return tag.find(" disabled=")==std::string::npos && tag.find(" disabled ")==std::string::npos && tag.find(" disabled>")==std::string::npos;
 }
 return false;
}
