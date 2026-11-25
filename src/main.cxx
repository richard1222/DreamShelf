// SPDX-License-Identifier: GPL-2.0-or-later
#include "catalog.h"
#include "catalog_cache.h"
#include "ui.h"
#include "common/pad.h"
#include "common/log.h"
#include "common/watchdog.h"
#include "common/bgm.h"
#include "launcher/font_text.h"
#include <SDL.h>
#include <curl/curl.h>
#include <net/net.h>
#include <sysutil/sysutil.h>
#include <sysmodule/sysmodule.h>
#include <lv2/process.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

static const char *data_dir="/dev_hdd0/game/THMS00001/USRDIR/";
// The single source of truth for the running version: it feeds both the
// curl User-Agent and the update checker, so the two can never drift.
#define DS_VERSION "0.1.9"
static const char *update_manifest_url="https://raw.githubusercontent.com/richard1222/DreamShelf/main/update.json";
// Used by one worker at a time; retaining the handle preserves DNS, TLS sessions
// and live connections across page fetches, redirects and later downloads.
static CURL *network_handle=nullptr;
static std::string cache_path(int source,unsigned page) {
 return "/dev_hdd0/tmp/themeshelf/catalog-v2-"+std::to_string(source)+"-"+std::to_string(page)+".txt";
}
static bool quitting=false;
static int screen_hidden=0;
static void system_event(u64 status,u64,void*) {
 if(status==SYSUTIL_EXIT_GAME)quitting=true;
 if(status==SYSUTIL_MENU_OPEN)screen_hidden=1;
 if(status==SYSUTIL_MENU_CLOSE)screen_hidden=0;
}
void a4_log(const char *tag,const char *fmt,...) {
 FILE *f=fopen("/dev_hdd0/tmp/themeshelf.log","a");if(!f)return;
 fprintf(f,"[%s] ",tag);va_list ap;va_start(ap,fmt);vfprintf(f,fmt,ap);va_end(ap);fputc('\n',f);fclose(f);
}
struct Job {
 unsigned page=1;bool more=false,enter_last=false;
 int source=0;bool download=false;Theme theme;std::vector<Theme> results;
 bool update_check=false,update_manual=false;
 std::string message,body;int done=0,cancel=0;unsigned progress=0;
 FILE *file=nullptr;size_t bytes=0,limit=0;
 int last_rc=0;char error[256]={};
 bool write_error=false,limit_hit=false;
};
static size_t receive(char *p,size_t size,size_t count,void *opaque) {
 Job *j=(Job*)opaque;
 if(size && count>j->limit/size){j->limit_hit=true;return 0;}
 size_t n=size*count;
 if(n>j->limit-j->bytes){j->limit_hit=true;return 0;}
 if(__atomic_load_n(&j->cancel,__ATOMIC_RELAXED))return 0;
 if(j->file) {if(fwrite(p,1,n,j->file)!=n){j->write_error=true;return 0;}} else j->body.append(p,n);
 j->bytes+=n;__atomic_store_n(&j->progress,(unsigned)j->bytes,__ATOMIC_RELAXED);return n;
}
static int progress(void *opaque,double,double,double,double) {
 return __atomic_load_n(&((Job*)opaque)->cancel,__ATOMIC_RELAXED)?1:0;
}
static bool get(Job *j,std::string url,bool binary) {
 for(int redirects=0;redirects<=4;++redirects) {
  if(!allowed_url(url)){j->message="Download host is not supported.";return false;}
  j->body.clear();j->bytes=0;j->limit=binary?48*1024*1024:4*1024*1024;
  if(j->file){rewind(j->file);if(ftruncate(fileno(j->file),0)!=0)return false;}
  CURL *c=network_handle;if(!c){j->message="Network could not start.";return false;}
  curl_easy_reset(c);
  std::string ca=std::string(data_dir)+"cacert.pem";
  j->error[0]=0;
  curl_easy_setopt(c,CURLOPT_URL,url.c_str());curl_easy_setopt(c,CURLOPT_USERAGENT,"DreamShelf/" DS_VERSION " PS3");
  curl_easy_setopt(c,CURLOPT_ACCEPT_ENCODING,"");
  curl_easy_setopt(c,CURLOPT_CAINFO,ca.c_str());curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
  curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,0L);curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
  curl_easy_setopt(c,CURLOPT_PROTOCOLS,(long)(CURLPROTO_HTTP|CURLPROTO_HTTPS));
  curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,8L);curl_easy_setopt(c,CURLOPT_TIMEOUT,binary?180L:30L);
  // Catalog hosts can be slow to send the first byte on a cold connection:
  // ps3-themes.com took ~6s to connect and ~14s total on its first fetch, and
  // a 15s low-speed window aborted a later fetch with rc=28 and 0 bytes.
  // Catalog fetches get a 45s low-speed window (still bounded by the 30s
  // hard timeout); downloads keep the tighter 15s window.
  curl_easy_setopt(c,CURLOPT_LOW_SPEED_LIMIT,512L);curl_easy_setopt(c,CURLOPT_LOW_SPEED_TIME,binary?15L:45L);
  curl_easy_setopt(c,CURLOPT_ERRORBUFFER,j->error);
  curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,receive);curl_easy_setopt(c,CURLOPT_WRITEDATA,j);
  curl_easy_setopt(c,CURLOPT_PROGRESSFUNCTION,progress);curl_easy_setopt(c,CURLOPT_PROGRESSDATA,j);curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);
  CURLcode rc=curl_easy_perform(c);long status=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&status);
  double total=0,connect=0,lookup=0,appconnect=0,pretransfer=0,starttransfer=0;long connections=0;
  curl_easy_getinfo(c,CURLINFO_TOTAL_TIME,&total);curl_easy_getinfo(c,CURLINFO_CONNECT_TIME,&connect);
  curl_easy_getinfo(c,CURLINFO_NAMELOOKUP_TIME,&lookup);curl_easy_getinfo(c,CURLINFO_APPCONNECT_TIME,&appconnect);
  curl_easy_getinfo(c,CURLINFO_PRETRANSFER_TIME,&pretransfer);curl_easy_getinfo(c,CURLINFO_STARTTRANSFER_TIME,&starttransfer);
  curl_easy_getinfo(c,CURLINFO_NUM_CONNECTS,&connections);
  j->last_rc=(int)rc;
  a4_log("network","url=%.120s source=%d binary=%d rc=%d http=%ld bytes=%u total=%.3f lookup=%.3f connect=%.3f appconnect=%.3f pretransfer=%.3f starttransfer=%.3f new_connections=%ld err=%s",url.c_str(),j->source,binary,(int)rc,status,(unsigned)j->bytes,total,lookup,connect,appconnect,pretransfer,starttransfer,connections,j->error);
  char *next=nullptr;curl_easy_getinfo(c,CURLINFO_REDIRECT_URL,&next);std::string redirect=next?next:"";
  if(rc!=CURLE_OK) {
   if(__atomic_load_n(&j->cancel,__ATOMIC_RELAXED))j->message="Cancelled.";
   else if(j->write_error)j->message="Could not write the theme file. Check HDD free space.";
   else if(j->limit_hit)j->message="Theme file is too large.";
   else j->message=std::string("Connection failed: ")+curl_easy_strerror(rc);
   return false;
  }
  if(status>=300 && status<400 && !redirect.empty()) {
   std::string target=resolve_url(url,redirect);
   if(target.empty()){j->message="Download host is not supported.";return false;}
   if(url.rfind("https://",0)==0 && target.rfind("https://",0)!=0){j->message="Secure download redirected to HTTP.";return false;}
   url=target;continue;
  }
  if(status!=200){char b[80];snprintf(b,sizeof b,"Site returned HTTP %ld. Try another source.",status);j->message=b;return false;}
  return true;
 }
 j->message="Too many redirects.";return false;
}
// Saber Pack "download": the theme file is bundled in the pkg, so
// installing is a local copy into /dev_hdd0/theme/ under its bundled
// name (an identical file already there is detected, exactly like a
// completed network download). Same .part + rename discipline and the
// same P3TF gate as the network path.
static void install_pack_theme(Job *j) {
 std::string file=j->theme.download.substr(7); // strip "pack://"
 if(file.empty() || file.find_first_of("/\\")!=std::string::npos || file.find("..")!=std::string::npos) {
  j->message="Bundled theme name is invalid.";return;
 }
 a4_log("pack","install start %.80s",file.c_str());
 mkdir("/dev_hdd0/theme",0777);
 std::string final="/dev_hdd0/theme/"+file,temp=final+".part";
 struct stat st;bool have=false;
 if(stat(final.c_str(),&st)==0) {
  FILE *old=fopen(final.c_str(),"rb");
  unsigned char header[64]={};size_t n=old?fread(header,1,sizeof header,old):0;
  if(old)fclose(old);
  have=p3t_header(header,n);
 }
 if(have){j->message="Already downloaded. Select it in XMB Theme Settings.";a4_log("pack","already downloaded %.80s",file.c_str());return;}
 std::string src=std::string(data_dir)+"pack/"+file;
 FILE *in=fopen(src.c_str(),"rb");
 if(!in){j->message="Bundled theme file is missing from the pack.";a4_log("pack","missing bundled file %.80s",file.c_str());return;}
 unsigned char header[64]={};size_t hn=fread(header,1,sizeof header,in);
 if(!p3t_header(header,hn)){fclose(in);j->message="Bundled file is not a P3T theme.";a4_log("pack","bundled file %.80s failed the P3T header check",file.c_str());return;}
 rewind(in);
 FILE *out=fopen(temp.c_str(),"wb");
 if(!out){fclose(in);j->message="Cannot create theme file. Check HDD free space.";return;}
 std::vector<unsigned char> buf(65536);size_t n;bool ok=true;size_t total=0;
 while((n=fread(buf.data(),1,buf.size(),in))>0) {
  if(__atomic_load_n(&j->cancel,__ATOMIC_RELAXED)){ok=false;break;}
  if(fwrite(buf.data(),1,n,out)!=n){ok=false;j->message="Could not write the theme file. Check HDD free space.";break;}
  total+=n;__atomic_store_n(&j->progress,(unsigned)total,__ATOMIC_RELAXED);
 }
 if(ferror(in))ok=false;
 if(fflush(out)!=0)ok=false;
 if(fclose(out)!=0)ok=false;
 fclose(in);
 j->bytes=total;
 if(__atomic_load_n(&j->cancel,__ATOMIC_RELAXED)){remove(temp.c_str());j->message="Cancelled.";a4_log("pack","install cancelled %.80s",file.c_str());return;}
 if(ok && rename(temp.c_str(),final.c_str())==0) {
  j->message="Installed! Quit, then open XMB > Theme Settings > Theme.";
  a4_log("pack","installed %.80s (%u bytes)",file.c_str(),(unsigned)total);
 } else {
  remove(temp.c_str());
  if(j->message.empty())j->message="Could not save theme.";
  a4_log("pack","install failed %.80s: %s",file.c_str(),j->message.c_str());
 }
}
static int worker(void *opaque) {
 Job *j=(Job*)opaque;
 if(j->update_check) {
  // Message-only update check: fetch the tiny manifest, compare, and
  // report. The automatic check stays silent unless a newer version
  // exists; a cancelled check never says anything.
  bool ok=get(j,update_manifest_url,false);
  std::string found=ok?parse_update_version(j->body):"";
  if(__atomic_load_n(&j->cancel,__ATOMIC_RELAXED))j->message.clear();
  else if(!found.empty() && update_is_newer(found,DS_VERSION))
   j->message="DreamShelf "+found+" is available - get it on Brewology or GitHub.";
  else if(j->update_manual)
   j->message=!found.empty()?"You are up to date (version " DS_VERSION ").":"Update check failed.";
  else j->message.clear();
  a4_log("update","manual=%d ok=%d found=%.16s message=%.80s",j->update_manual,ok,found.c_str(),j->message.c_str());
 } else if(!j->download) {
  bool ok=get(j,catalog_page_url(j->source,j->page),false);
  if(!ok && !__atomic_load_n(&j->cancel,__ATOMIC_RELAXED) &&
     (j->last_rc==CURLE_OPERATION_TIMEDOUT || j->last_rc==CURLE_COULDNT_RESOLVE_HOST || j->last_rc==CURLE_COULDNT_CONNECT)) {
   a4_log("network","source=%d retrying catalog fetch once after rc=%d",j->source,j->last_rc);
   ok=get(j,catalog_page_url(j->source,j->page),false);
  }
  if(ok) {
   j->results=parse_catalog(j->source,j->body);
   j->more=catalog_has_next(j->source,j->body);
   if(j->results.empty())j->message="No themes found. Try another source.";
   else j->message="Choose a theme. X downloads; L1/R1 changes source.";
  }
 } else if(j->source==3 || j->theme.download.rfind("pack://",0)==0) {
  install_pack_theme(j);
 } else {
  std::string url=j->theme.download;
  if(url.empty() && get(j,j->theme.page,false)) {
   url=parse_download(j->body,j->theme.page);
   if(url.empty()) {
    url=themezer_download_url(j->theme.page);
    if(!url.empty())a4_log("download","no link on theme page; using Themezer API fallback for %.80s",j->theme.title.c_str());
   }
  }
  if(url.empty()) {
   if(j->message.empty())j->message="No direct P3T download found on this page.";
   a4_log("download","no download URL for %.80s (page %.120s)",j->theme.title.c_str(),j->theme.page.c_str());
  }
  else {
   mkdir("/dev_hdd0/theme",0777);
   std::string final="/dev_hdd0/theme/"+filename_for(j->theme),temp=final+".part";
   struct stat st;bool have=false;
   if(stat(final.c_str(),&st)==0) {
    // A file with this name counts as "already downloaded" only if it is
    // really a P3T: a corrupt or zero-byte file left behind must not
    // block a fresh download forever.
    FILE *old=fopen(final.c_str(),"rb");
    unsigned char header[64]={};size_t n=old?fread(header,1,sizeof header,old):0;
    if(old)fclose(old);
    have=p3t_header(header,n);
    if(!have)a4_log("download","existing file %.80s failed the P3T header check; re-downloading",final.c_str());
   }
   if(have)j->message="Already downloaded. Select it in XMB Theme Settings.";
   else {
    a4_log("download","downloading %.80s -> %.80s",j->theme.title.c_str(),final.c_str());
    j->file=fopen(temp.c_str(),"wb+");
    if(!j->file){j->message="Cannot create theme file. Check HDD free space.";a4_log("download","cannot create temp file %.80s",temp.c_str());}
    else {
     bool ok=get(j,url,true);unsigned char header[64]={};bool save_error=false;
     if(fflush(j->file)!=0){ok=false;save_error=true;}
     rewind(j->file);size_t n=fread(header,1,sizeof header,j->file);
     if(!p3t_header(header,n)){if(ok)j->message="Download is not a P3T theme.";ok=false;}
     if(fclose(j->file)!=0){ok=false;save_error=true;}
     j->file=nullptr;
     if(__atomic_load_n(&j->cancel,__ATOMIC_RELAXED))ok=false;
     if(ok && rename(temp.c_str(),final.c_str())==0) {
      j->message="Downloaded! Quit, then open XMB > Theme Settings > Theme.";
      a4_log("download","saved %.80s (%u bytes)",final.c_str(),(unsigned)j->bytes);
     } else {
      remove(temp.c_str());
      if(ok)j->message="Could not save theme.";
      else if(save_error && j->message.empty())j->message="Could not save the theme file.";
      else if(j->message.empty())j->message="Download failed.";
      if(__atomic_load_n(&j->cancel,__ATOMIC_RELAXED))a4_log("download","cancelled %.80s",j->theme.title.c_str());
      else a4_log("download","failed %.80s: %s",j->theme.title.c_str(),j->message.c_str());
     }
    }
   }
  }
 }
 __atomic_store_n(&j->done,1,__ATOMIC_RELEASE);return 0;
}
int main() {
 a4_log("boot","DreamShelf " DS_VERSION ": entered main");
 sysModuleLoad(SYSMODULE_NET);netInitialize();curl_global_init(CURL_GLOBAL_DEFAULT);
 a4_log("boot","network libraries initialized");
 network_handle=curl_easy_init();
 sysUtilRegisterCallback(0,system_event,nullptr);
 a4_log("boot","before SDL_Init");
 if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)!=0){a4_log("boot","SDL_Init failed: %s",SDL_GetError());return 1;}
 a4_log("boot","SDL_Init returned; starting watchdog before window creation");
 a4_watchdog_start("dreamshelf");
 a4_watchdog_phase(A4_PHASE_BOOT);
 a4_log("boot","before window creation");
 SDL_Window *w=SDL_CreateWindow("DreamShelf",0,0,1280,720,SDL_WINDOW_FULLSCREEN);
 a4_log("boot","window=%p; before software renderer",(void*)w);
 SDL_Renderer *r=w?SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE):nullptr;
 a4_log("boot","renderer=%p err=%s",(void*)r,SDL_GetError());
 if(!r){a4_watchdog_stop();SDL_Quit();return 1;}
 SDL_RenderSetLogicalSize(r,1280,720);a4_pad_init();
 a4_log("boot","before font atlas");auto font=a4_font_create(r);
 a4_log("boot","font=%p",(void*)font);
 if(!font){a4_watchdog_stop();SDL_DestroyRenderer(r);SDL_DestroyWindow(w);SDL_Quit();return 1;}
 a4_watchdog_tick();a4_watchdog_phase(A4_PHASE_DRAW);
 SDL_SetRenderDrawColor(r,239,241,242,255);SDL_RenderClear(r);
 a4_draw_text(font,r,64,72,"DreamShelf - starting...",3,45,49,54);
 a4_log("boot","before first simple present");a4_watchdog_phase(A4_PHASE_PRESENT);
 SDL_RenderPresent(r);a4_log("boot","first simple present returned");SDL_Delay(20);a4_watchdog_tick();
 a4_watchdog_phase(A4_PHASE_LOAD);
 mkdir("/dev_hdd0/tmp/themeshelf",0777);
 CatalogCache catalogs[4];
 unsigned pages[4]={1,1,1,1},loaded_pages[4]={0,0,0,0};
 std::vector<Theme> themes;int source=0,selected=0;uint32_t held=0;bool dirty=true;
 std::string message;Job *job=nullptr;SDL_Thread *thread=nullptr;
 // Pending actions: input stays live while a worker runs. In 0.1.4 every
 // button except Circle was dropped until the fetch finished, which on the
 // slow catalog host took 14-22s and looked like a dead controller. A load
 // or download requested mid-job is recorded here and executed when the
 // worker is joined, so the single-worker / single-CURL-handle invariant
 // still holds.
 bool pending_load=false,pending_force=false,pending_enter_last=false;
 bool pending_download=false;Theme pending_theme;
 bool pending_update=false;
 // The automatic update check runs once, after the first catalog load
 // has settled (cache hit or finished fetch), and only while no other
 // job is running, so it can never delay browsing.
 bool first_load_settled=false,update_checked=false;
 auto load_source=[&](bool force,bool enter_last=false) {
  if(source==3) {
   // Saber Pack: the list is built locally from the bundled files.
   // No disk cache, no network; Triangle simply rebuilds the list.
   catalogs[3]=CatalogCache{};
   catalogs[3].entries=pack_catalog(std::string(data_dir)+"pack/");
   catalogs[3].fetched=(uint64_t)time(nullptr);
   loaded_pages[3]=pages[3];
   themes=catalogs[3].entries;selected=enter_last && !themes.empty()?(int)themes.size()-1:0;
   message=themes.empty()?"Saber Pack has no themes in this build.":"Bundled Saber Pack themes. X installs; no network needed.";
   a4_log("pack","list built entries=%u",(unsigned)themes.size());
   first_load_settled=true;
   return;
  }
  if(loaded_pages[source]!=pages[source]) {
   catalogs[source]=CatalogCache{};
   uint64_t now=(uint64_t)time(nullptr);
   bool disk=read_catalog_cache(cache_path(source,pages[source]),source,catalogs[source]);
   if(!disk)a4_log("cache","source=%d page=%u miss; fetching from network",source,pages[source]);
   else if(!catalogs[source].fresh(now))a4_log("cache","source=%d page=%u stale age=%llus; refreshing",source,pages[source],(unsigned long long)(now>=catalogs[source].fetched?now-catalogs[source].fetched:0));
   loaded_pages[source]=pages[source];
  }
  themes=catalogs[source].entries;selected=enter_last && !themes.empty()?(int)themes.size()-1:0;
  if(!force && catalogs[source].fresh((uint64_t)time(nullptr))) {
   message="Cached catalog ready. Triangle checks for new themes.";
   a4_log("cache","source=%d hit entries=%u network_requests=0",source,(unsigned)themes.size());
   first_load_settled=true;
   return;
  }
  job=new Job;job->source=source;job->page=pages[source];job->enter_last=enter_last;
  message=themes.empty()?"Loading catalog...":"Cached catalog ready; checking for updates...";
  thread=SDL_CreateThread(worker,"catalog",job);
  if(!thread){delete job;job=nullptr;message="Could not create network worker.";}
 };
 auto start_download=[&](const Theme &t) {
  job=new Job;job->source=source;job->download=true;job->theme=t;
  message="Downloading theme... Circle cancels.";
  thread=SDL_CreateThread(worker,"network",job);
  if(!thread){delete job;job=nullptr;message="Could not create network worker.";}
 };
 auto start_update_check=[&](bool manual) {
  job=new Job;job->source=source;job->update_check=true;job->update_manual=manual;
  if(manual)message="Checking for updates...";
  thread=SDL_CreateThread(worker,"update",job);
  if(!thread){delete job;job=nullptr;if(manual)message="Could not create network worker.";}
 };
 auto request_load=[&](bool force,bool enter_last) {
  // A catalog job in flight is cancelled and the load runs when the worker
  // is joined; a download in flight is left alone and the load queues behind it.
  if(!job){load_source(force,enter_last);return;}
  bool was_download=job->download;
  if(!was_download)__atomic_store_n(&job->cancel,1,__ATOMIC_RELAXED);
  pending_load=true;pending_force=force;pending_enter_last=enter_last;pending_download=false;
  if(!was_download)message="Loading catalog...";
 };
 std::string bgm_path=std::string(data_dir)+"bgm_startup.wav";
 a4_bgm_start(bgm_path.c_str());
 load_source(false);a4_log("boot","catalog started; entering menu loop");
 while(!quitting || job) {
  a4_watchdog_tick();a4_watchdog_phase(A4_PHASE_SYSUTIL);
  sysUtilCheckCallback();SDL_Event ev;while(SDL_PollEvent(&ev))if(ev.type==SDL_QUIT)quitting=true;
  a4_watchdog_phase(A4_PHASE_INPUT);
  A4Pad pad;a4_pad_poll(&pad);
  if(pad.buttons!=held)a4_log("pad","buttons=0x%08x lx=%d ly=%d present=%d port=%d",(unsigned)pad.buttons,(int)pad.lx,(int)pad.ly,pad.present,pad.port);
  uint32_t pressed=pad.buttons&~held;held=pad.buttons;
  if(job && __atomic_load_n(&job->done,__ATOMIC_ACQUIRE)) {
   SDL_WaitThread(thread,nullptr);
   int jsrc=job->source;unsigned jpage=job->page;bool jdl=job->download;bool jenter=job->enter_last;bool jupd=job->update_check;
   if(!jdl && !jupd && !job->results.empty()) {
    catalogs[jsrc].entries=job->results;catalogs[jsrc].more=job->more;
    catalogs[jsrc].fetched=(uint64_t)time(nullptr);
    if(!write_catalog_cache(cache_path(jsrc,jpage),jsrc,catalogs[jsrc]))a4_log("cache","source=%d disk save failed; memory cache retained",jsrc);
    // The user may have switched source/page while this fetch ran; only
    // swap the results onto the screen if they are for what is showing.
    if(jsrc==source && jpage==pages[source]){themes=catalogs[jsrc].entries;selected=jenter?(int)themes.size()-1:0;}
   }
   if(!jdl && !jupd)first_load_settled=true;
   // An update job only replaces the message when it has something to
   // say (a newer version, or the result of a manual check); a silent
   // automatic check leaves the catalog message untouched.
   if(!jupd || !job->message.empty())message=job->message;
   delete job;job=nullptr;thread=nullptr;dirty=true;
   if(!quitting) {
    if(pending_download){pending_download=false;Theme t=pending_theme;pending_theme=Theme{};if(!t.title.empty())start_download(t);}
    else if(pending_load){pending_load=false;bool f=pending_force;bool e=pending_enter_last;pending_force=false;pending_enter_last=false;load_source(f,e);}
    else if(pending_update){pending_update=false;start_update_check(true);}
   } else {pending_load=false;pending_download=false;pending_update=false;}
  }
  // Once per launch, after the first catalog load has settled and no
  // other job is running, quietly check for a newer DreamShelf.
  if(!quitting && !update_checked && first_load_settled && !job && !pressed) {
   update_checked=true;start_update_check(false);
  }
  if(quitting && job)__atomic_store_n(&job->cancel,1,__ATOMIC_RELAXED);
  if(!quitting && pressed) {
   if(pressed&A4B_BIT(A4B_CIRCLE)){
    if(job && job->update_check && !job->update_manual){__atomic_store_n(&job->cancel,1,__ATOMIC_RELAXED);pending_load=false;pending_download=false;pending_update=false;quitting=true;}
    else if(job){__atomic_store_n(&job->cancel,1,__ATOMIC_RELAXED);pending_load=false;pending_download=false;pending_update=false;message="Cancelling...";}
    else quitting=true;
   }
   // Every other button works whether or not a worker is running: navigation
   // applies to the entries on screen immediately, and loads/downloads
   // requested mid-job are queued behind the running worker.
   bool load=false,force=false,enter_last=false;
   if(pressed&A4B_BIT(A4B_R1)){source=(source+1)%4;themes.clear();selected=0;load=true;}
   if(pressed&A4B_BIT(A4B_L1)){source=(source+3)%4;themes.clear();selected=0;load=true;}
   if(pressed&A4B_BIT(A4B_TRIANGLE)){load=true;force=true;}
   if(pressed&A4B_BIT(A4B_SQUARE)){
    if(!job)start_update_check(true);
    else if(!job->update_check)pending_update=true;
    else if(job->update_check)message="Checking for updates...";
   }
   if(!load && !themes.empty()) {
    bool next=(pressed&A4B_BIT(A4B_R2)) || ((pressed&A4B_BIT(A4B_DOWN)) && selected==(int)themes.size()-1) || ((pressed&A4B_BIT(A4B_RIGHT)) && selected==(int)themes.size()-1);
    bool prev=(pressed&A4B_BIT(A4B_L2)) || ((pressed&A4B_BIT(A4B_UP)) && selected==0);
    if(next) {
     if(catalogs[source].more){++pages[source];load=true;}
     else message="End of this site's catalog.";
    } else if(prev) {
     if(pages[source]>1){--pages[source];load=true;enter_last=true;}
    } else {
    if(pressed&A4B_BIT(A4B_DOWN))selected=std::min(selected+1,(int)themes.size()-1);
    if(pressed&A4B_BIT(A4B_UP))selected=std::max(selected-1,0);
    if(pressed&A4B_BIT(A4B_RIGHT))selected=std::min(selected+10,(int)themes.size()-1);
    if(pressed&A4B_BIT(A4B_LEFT))selected=std::max(selected-10,0);
    }
   }
   // A failed page fetch leaves the page number intact so it can be retried,
   // or left with L2 even when it has no entries yet.
   if(!load && themes.empty() && (pressed&A4B_BIT(A4B_L2)) && pages[source]>1){--pages[source];load=true;enter_last=true;}
   if(load)request_load(force,enter_last);
   else if((pressed&A4B_BIT(A4B_CROSS)) && !themes.empty()) {
    if(!job)start_download(themes[selected]);
    else if(!job->download){pending_theme=themes[selected];pending_download=true;pending_load=false;__atomic_store_n(&job->cancel,1,__ATOMIC_RELAXED);message="Downloading theme... Circle cancels.";}
    // A download already in flight: X is ignored until it finishes.
   }
   dirty=true;
  }
  static unsigned displayed_bytes=0;static uint32_t last_progress_draw=0;
  unsigned bytes=job?__atomic_load_n(&job->progress,__ATOMIC_RELAXED):0;
  if(job && bytes!=displayed_bytes && SDL_GetTicks()-last_progress_draw>=500) {
   displayed_bytes=bytes;last_progress_draw=SDL_GetTicks();dirty=true;
  }
  static int was_hidden=0;if(was_hidden && !screen_hidden)dirty=true;was_hidden=screen_hidden;
  if(dirty && !screen_hidden && !quitting) {
   static unsigned draw_count=0;
   bool trace=draw_count<3;
   if(trace)a4_log("draw","frame=%u before UI draw",draw_count);
   a4_watchdog_phase(A4_PHASE_DRAW);
   draw_theme_ui(r,font,source,pages[source],themes,selected,message,job!=nullptr,job?__atomic_load_n(&job->progress,__ATOMIC_RELAXED):0);
   if(trace)a4_log("draw","frame=%u UI returned; before present",draw_count);
   a4_watchdog_phase(A4_PHASE_PRESENT);SDL_RenderPresent(r);dirty=false;
   if(trace)a4_log("draw","frame=%u present returned",draw_count);
   ++draw_count;
  }
  a4_watchdog_phase(A4_PHASE_DELAY);SDL_Delay(16);
 }
 a4_bgm_stop();a4_watchdog_stop();a4_font_destroy(font);SDL_DestroyRenderer(r);SDL_DestroyWindow(w);SDL_Quit();
 curl_easy_cleanup(network_handle);curl_global_cleanup();netDeinitialize();return 0;
}
