// SPDX-License-Identifier: GPL-2.0-or-later
#include "catalog.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <set>
const Source sources[4] = {
 {"PS3-Themes", "http://www.ps3-themes.com/"},
 {"Themezer", "https://themezer.net/ps3/themes"},
 {"Curated backup", "https://whatwhatboy.com/console/ps3/themes.html"},
 {"Saber Pack", "pack://local"}
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
// Resolve ref against base. Absolute URLs pass through unchanged;
// protocol-relative (//host/path) takes the base scheme; root-relative
// (/path) takes the base scheme+host; bare relative paths resolve
// against the base URL's directory. Fragments, unknown schemes
// (javascript:, data:) and unresolvable refs yield empty: the caller
// re-checks the result with allowed_url() either way.
std::string resolve_url(const std::string &base, const std::string &ref) {
 if(ref.empty())return {};
 if(ref.find("://")!=std::string::npos)return ref;
 size_t colon=ref.find(':');
 if(colon!=std::string::npos) {
  size_t delim=ref.find_first_of("/?#");
  if(delim==std::string::npos || colon<delim)return {};
 }
 size_t p=base.find("://");if(p==std::string::npos)return {};
 std::string scheme=base.substr(0,p);
 size_t host_begin=p+3,end=base.find_first_of("/?#",host_begin);
 std::string host=base.substr(host_begin,end==std::string::npos?end:end-host_begin);
 if(host.empty())return {};
 if(ref.rfind("//",0)==0)return scheme+":"+ref;
 if(ref[0]=='#')return {};
 if(ref[0]=='?') {
  std::string b=base;size_t q=b.find('?');if(q!=std::string::npos)b.resize(q);
  size_t h=b.find('#');if(h!=std::string::npos)b.resize(h);
  return b+ref;
 }
 if(ref[0]=='/')return scheme+"://"+host+ref;
 size_t slash=base.rfind('/');
 if(slash!=std::string::npos && slash>=host_begin)return base.substr(0,slash+1)+ref;
 return scheme+"://"+host+"/"+ref;
}
// Themezer download fallback. Themezer detail pages expose downloads
// through the API endpoint https://api.themezer.net/ps3/themes/<ID>/download
// (the same URLs the curated catalog embeds in data-original-href), and
// the theme ID is the trailing segment of the page slug: all 28 curated
// entries pair slug ...-<ID> with API URL .../themes/<ID>/download.
// Used only when the detail page yields no download link at all, so a
// wrong guess degrades to a visible HTTP error, never a silent failure.
std::string themezer_download_url(const std::string &page_url) {
 const std::string prefix="https://themezer.net/ps3/themes/";
 if(page_url.rfind(prefix,0)!=0)return {};
 std::string slug=page_url.substr(prefix.size());
 size_t cut=slug.find_first_of("?#");if(cut!=std::string::npos)slug.resize(cut);
 while(!slug.empty() && slug.back()=='/')slug.pop_back();
 size_t dash=slug.rfind('-');if(dash==std::string::npos)return {};
 std::string id=slug.substr(dash+1);
 if(id.size()<3 || id.size()>12)return {};
 bool digit=false;
 for(unsigned char c:id){if(!isalnum(c))return {};if(isdigit(c))digit=true;}
 if(!digit)return {};
 return "https://api.themezer.net/ps3/themes/"+id+"/download?src=webapp";
}
std::string parse_download(const std::string &html, const std::string &base_url) {
 size_t i=0;
 while((i=html.find("<a",i))!=std::string::npos) {
  size_t end=html.find('>',i);if(end==std::string::npos)break;
  auto url=attribute(html.substr(i,end-i+1),"href");i=end+1;
  url=resolve_url(base_url,url);
  if(allowed_url(url) && (url.find(".p3t")!=std::string::npos || url.find("/download")!=std::string::npos))return url;
 }
 return {};
}
std::vector<Theme> parse_catalog(int source, const std::string &html) {
 std::vector<Theme> out;std::set<std::string> seen;
 if(source<0 || source>3 || html.size()>4*1024*1024)return out;
 // Source 3 is the bundled Saber Pack: its list comes from
 // pack_catalog(), never from parsed HTML.
 if(source==3)return out;
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
 if(source<0 || source>3 || page==0)return {};
 if(page==1)return sources[source].url;
 if(source==3)return {};
 if(source==0)return "http://www.ps3-themes.com/page/"+std::to_string(page);
 if(source==1)return "https://themezer.net/ps3/themes?page="+std::to_string(page);
 return {};
}
bool catalog_has_next(int source,const std::string &html) {
 if(source<0 || source>3)return false;
 if(source==2 || source==3)return false;
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
// Saber Pack manifest: pretty title + bundled filename per entry.
// Hardcoded on purpose: titles stay presentable and the order stable.
// Entries whose file is absent from the pkg (or fails the P3TF check)
// are skipped, so a partial pack still lists exactly what it ships.
struct PackItem { const char *title, *file; };
static const PackItem pack_items[] = {
 {"Saber Royal HD", "Saber-Royal-HD-v1.3.p3t"},
 {"Dreamcast Nostalgia", "Dreamcast-Nostalgia.p3t"},
 {"Neon Xbox Green", "Neon-Xbox-Green.p3t"},
 {"Saber Alter Black", "Saber-Alter-Black.p3t"},
 {"Midnight Carbon", "Midnight-Carbon.p3t"},
 {"Vocaloid Teal", "Vocaloid-Teal.p3t"}
};
std::vector<Theme> pack_catalog(const std::string &pack_dir) {
 std::vector<Theme> out;
 for(const auto &item:pack_items) {
  FILE *f=fopen((pack_dir+item.file).c_str(),"rb");
  if(!f)continue;
  unsigned char header[64]={};size_t n=fread(header,1,sizeof header,f);fclose(f);
  if(!p3t_header(header,n))continue;
  std::string ref=std::string("pack://")+item.file;
  out.push_back({item.title,ref,ref});
 }
 return out;
}
std::string parse_update_version(const std::string &json) {
 size_t key=json.find("\"version\"");if(key==std::string::npos)return {};
 size_t colon=json.find(':',key+9);if(colon==std::string::npos)return {};
 size_t open=json.find('"',colon+1);if(open==std::string::npos)return {};
 size_t close=json.find('"',open+1);if(close==std::string::npos)return {};
 std::string v=json.substr(open+1,close-open-1);
 return v.size()<=16?v:std::string{};
}
static bool version_parts(const std::string &s,int parts[3]) {
 parts[0]=parts[1]=parts[2]=0;
 size_t i=(!s.empty() && (s[0]=='v' || s[0]=='V'))?1:0;
 for(int p=0;p<3;++p) {
  if(i>=s.size())return p>0;
  if(!isdigit((unsigned char)s[i]))return false;
  long value=0;
  while(i<s.size() && isdigit((unsigned char)s[i])) {value=value*10+(s[i]-'0');if(value>100000)return false;++i;}
  parts[p]=(int)value;
  if(i<s.size()) {if(s[i]!='.')return false;++i;}
 }
 return i==s.size();
}
bool update_is_newer(const std::string &latest,const std::string &current) {
 int a[3],b[3];
 if(!version_parts(latest,a) || !version_parts(current,b))return false;
 for(int i=0;i<3;++i)if(a[i]!=b[i])return a[i]>b[i];
 return false;
}
