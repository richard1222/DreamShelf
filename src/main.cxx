// SPDX-License-Identifier: GPL-2.0-or-later
#include "catalog.h"
#include "catalog_cache.h"
#include "ui.h"
#include "common/pad.h"
#include "common/log.h"
#include "common/watchdog.h"
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
 std::string message,body;int done=0,cancel=0;unsigned progress=0;
 FILE *file=nullptr;size_t bytes=0,limit=0;
};
static size_t receive(char *p,size_t size,size_t count,void *opaque) {
 Job *j=(Job*)opaque;if(size && count>j->limit/size)return 0;
 size_t n=size*count;if(n>j->limit-j->bytes || __atomic_load_n(&j->cancel,__ATOMIC_RELAXED))return 0;
 if(j->file) {if(fwrite(p,1,n,j->file)!=n)return 0;} else j->body.append(p,n);
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
  curl_easy_setopt(c,CURLOPT_URL,url.c_str());curl_easy_setopt(c,CURLOPT_USERAGENT,"DreamShelf/0.1.4 PS3");
  curl_easy_setopt(c,CURLOPT_ACCEPT_ENCODING,"");
  curl_easy_setopt(c,CURLOPT_CAINFO,ca.c_str());curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
  curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,0L);curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
  curl_easy_setopt(c,CURLOPT_PROTOCOLS,(long)(CURLPROTO_HTTP|CURLPROTO_HTTPS));
  curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,8L);curl_easy_setopt(c,CURLOPT_TIMEOUT,binary?180L:30L);
  curl_easy_setopt(c,CURLOPT_LOW_SPEED_LIMIT,512L);curl_easy_setopt(c,CURLOPT_LOW_SPEED_TIME,15L);
  curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,receive);curl_easy_setopt(c,CURLOPT_WRITEDATA,j);
  curl_easy_setopt(c,CURLOPT_PROGRESSFUNCTION,progress);curl_easy_setopt(c,CURLOPT_PROGRESSDATA,j);curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);
  CURLcode rc=curl_easy_perform(c);long status=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&status);
  double total=0,connect=0;long connections=0;
  curl_easy_getinfo(c,CURLINFO_TOTAL_TIME,&total);curl_easy_getinfo(c,CURLINFO_CONNECT_TIME,&connect);
  curl_easy_getinfo(c,CURLINFO_NUM_CONNECTS,&connections);
  a4_log("network","source=%d binary=%d rc=%d http=%ld bytes=%u total=%.3f connect=%.3f new_connections=%ld",j->source,binary,(int)rc,status,(unsigned)j->bytes,total,connect,connections);
  char *next=nullptr;curl_easy_getinfo(c,CURLINFO_REDIRECT_URL,&next);std::string redirect=next?next:"";
  if(rc!=CURLE_OK){j->message=__atomic_load_n(&j->cancel,__ATOMIC_RELAXED)?"Cancelled.":std::string("Connection failed: ")+curl_easy_strerror(rc);return false;}
  if(status>=300 && status<400 && !redirect.empty()) {
   if(url.rfind("https://",0)==0 && redirect.rfind("https://",0)!=0){j->message="Secure download redirected to HTTP.";return false;}
   url=redirect;continue;
  }
  if(status!=200){char b[80];snprintf(b,sizeof b,"Site returned HTTP %ld. Try another source.",status);j->message=b;return false;}
  return true;
 }
 j->message="Too many redirects.";return false;
}
static int worker(void *opaque) {
 Job *j=(Job*)opaque;
 if(!j->download) {
  if(get(j,catalog_page_url(j->source,j->page),false)) {
   j->results=parse_catalog(j->source,j->body);
   j->more=catalog_has_next(j->source,j->body);
   if(j->results.empty())j->message="No themes found. Try another source.";
   else j->message="Choose a theme. X downloads; L1/R1 changes source.";
  }
 } else {
  std::string url=j->theme.download;
  if(url.empty() && get(j,j->theme.page,false))url=parse_download(j->body);
  if(url.empty()) {if(j->message.empty())j->message="No direct P3T download found on this page.";}
  else {
   mkdir("/dev_hdd0/theme",0777);
   std::string final="/dev_hdd0/theme/"+filename_for(j->theme),temp=final+".part";
   struct stat st;
   if(stat(final.c_str(),&st)==0)j->message="Already downloaded. Select it in XMB Theme Settings.";
   else {
    j->file=fopen(temp.c_str(),"wb+");
    if(!j->file)j->message="Cannot create theme file. Check HDD free space.";
    else {
     bool ok=get(j,url,true);unsigned char header[64]={};
     if(fflush(j->file)!=0)ok=false;
     rewind(j->file);size_t n=fread(header,1,sizeof header,j->file);
     if(!p3t_header(header,n)){if(ok)j->message="Download is not a P3T theme.";ok=false;}
     if(fclose(j->file)!=0)ok=false;
     j->file=nullptr;
     if(__atomic_load_n(&j->cancel,__ATOMIC_RELAXED))ok=false;
     if(ok && rename(temp.c_str(),final.c_str())==0)j->message="Downloaded! Quit, then open XMB > Theme Settings > Theme.";
     else {remove(temp.c_str());if(ok)j->message="Could not save theme.";}
    }
   }
  }
 }
 __atomic_store_n(&j->done,1,__ATOMIC_RELEASE);return 0;
}
int main() {
 a4_log("boot","DreamShelf 0.1.4 diagnostic: entered main");
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
 CatalogCache catalogs[3];
 unsigned pages[3]={1,1,1},loaded_pages[3]={0,0,0};
 std::vector<Theme> themes;int source=0,selected=0;uint32_t held=0;bool dirty=true;
 std::string message;Job *job=nullptr;SDL_Thread *thread=nullptr;
 auto load_source=[&](bool force,bool enter_last=false) {
  if(loaded_pages[source]!=pages[source]) {
   catalogs[source]=CatalogCache{};
   read_catalog_cache(cache_path(source,pages[source]),source,catalogs[source]);
   loaded_pages[source]=pages[source];
  }
  themes=catalogs[source].entries;selected=enter_last && !themes.empty()?(int)themes.size()-1:0;
  if(!force && catalogs[source].fresh((uint64_t)time(nullptr))) {
   message="Cached catalog ready. Triangle checks for new themes.";
   a4_log("cache","source=%d hit entries=%u network_requests=0",source,(unsigned)themes.size());
   return;
  }
  job=new Job;job->source=source;job->page=pages[source];job->enter_last=enter_last;
  message=themes.empty()?"Loading catalog...":"Cached catalog ready; checking for updates...";
  thread=SDL_CreateThread(worker,"catalog",job);
  if(!thread){delete job;job=nullptr;message="Could not create network worker.";}
 };
 load_source(false);a4_log("boot","catalog started; entering menu loop");
 while(!quitting || job) {
  a4_watchdog_tick();a4_watchdog_phase(A4_PHASE_SYSUTIL);
  sysUtilCheckCallback();SDL_Event ev;while(SDL_PollEvent(&ev))if(ev.type==SDL_QUIT)quitting=true;
  a4_watchdog_phase(A4_PHASE_INPUT);
  A4Pad pad;a4_pad_poll(&pad);uint32_t pressed=pad.buttons&~held;held=pad.buttons;
  if(job && __atomic_load_n(&job->done,__ATOMIC_ACQUIRE)) {
   SDL_WaitThread(thread,nullptr);
   if(!job->download && !job->results.empty()) {
    themes.swap(job->results);selected=job->enter_last?(int)themes.size()-1:0;catalogs[source].entries=themes;
    catalogs[source].more=job->more;
    catalogs[source].fetched=(uint64_t)time(nullptr);
    if(!write_catalog_cache(cache_path(source,pages[source]),source,catalogs[source]))a4_log("cache","source=%d disk save failed; memory cache retained",source);
   }
   message=job->message;delete job;job=nullptr;thread=nullptr;dirty=true;
  }
  if(quitting && job)__atomic_store_n(&job->cancel,1,__ATOMIC_RELAXED);
  if(!quitting && pressed) {
   if(pressed&A4B_BIT(A4B_CIRCLE)){if(job){__atomic_store_n(&job->cancel,1,__ATOMIC_RELAXED);message="Cancelling...";}else quitting=true;}
   if(!job) {
    bool load=false,force=false,enter_last=false;
    if(pressed&A4B_BIT(A4B_R1)){source=(source+1)%3;themes.clear();selected=0;load=true;}
    if(pressed&A4B_BIT(A4B_L1)){source=(source+2)%3;themes.clear();selected=0;load=true;}
    if(pressed&A4B_BIT(A4B_TRIANGLE)){load=true;force=true;}
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
    if(load)load_source(force,enter_last);
    else if((pressed&A4B_BIT(A4B_CROSS)) && !themes.empty()) {
     job=new Job;job->source=source;job->download=true;job->theme=themes[selected];
     message="Downloading theme... Circle cancels.";
     thread=SDL_CreateThread(worker,"network",job);
     if(!thread){delete job;job=nullptr;message="Could not create network worker.";}
    }
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
 a4_watchdog_stop();a4_font_destroy(font);SDL_DestroyRenderer(r);SDL_DestroyWindow(w);SDL_Quit();
 curl_easy_cleanup(network_handle);curl_global_cleanup();netDeinitialize();return 0;
}
