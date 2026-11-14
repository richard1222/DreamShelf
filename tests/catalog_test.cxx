#include "catalog.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>
static std::string read(const char *p){std::ifstream f(p);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char **argv) {
 assert(argc==5 || argc==7 || argc==9);
 auto original=parse_catalog(0,read(argv[1]));auto themezer=parse_catalog(1,read(argv[2]));auto curated=parse_catalog(2,read(argv[3]));
 assert(original.size()>=10 && themezer.size()>=10 && curated.size()==28);
 auto download=parse_download(read(argv[4]));assert(download.find("JUJU7U_K413N")!=std::string::npos);
 assert(original[0].title=="Jujutsu Kaisen (Slideshow)");
 assert(!themezer[0].title.empty());assert(curated[0].title=="PS4 XMB 2.0");
 assert(!allowed_url("http://www.ps3-themes.com@evil.com/a.p3t"));
 assert(!allowed_url("file:///dev_flash/a"));assert(!allowed_url("https://127.0.0.1/a"));
 assert(!allowed_url("http://download.ps3-themes.com:8080/a.p3t"));
 assert(parse_download("<a href='https://evil.com/a.p3t'>Download</a>").empty());
 assert(parse_catalog(0,"<h2 class='entry-title'><a href='http://www.ps3-themes.com/test.html'>A &amp; B</a></h2>")[0].title=="A & B");
 assert(filename_for({"../../evil", "https://themezer.net/a", ""}).find('/')==std::string::npos);
 unsigned char header[64]={'P','3','T','F'};assert(p3t_header(header,64));assert(!p3t_header(header,4));header[0]='<';assert(!p3t_header(header,64));
 assert(catalog_page_url(0,2)=="http://www.ps3-themes.com/page/2");
 assert(catalog_page_url(1,626)=="https://themezer.net/ps3/themes?page=626");
 assert(catalog_page_url(2,2).empty() && catalog_page_url(0,0).empty());
 assert(catalog_has_next(0,read(argv[1])) && catalog_has_next(1,read(argv[2])));
 assert(!catalog_has_next(2,read(argv[3])));
 assert(!catalog_has_next(1,"<button aria-label=\"Next Page\" disabled>Next</button>"));
 assert(!catalog_has_next(1,"<button disabled=\"\" aria-label=\"Next Page\">Next</button>"));
 assert(!catalog_has_next(0,"<a rel=\"next\" href=\"https://evil.com/page/2\">Next</a>"));
 if(argc>=7) {
  auto second_original=parse_catalog(0,read(argv[5])),second_themezer=parse_catalog(1,read(argv[6]));
  assert(second_original.size()>=10 && second_themezer.size()>=10);
  for(const auto &a:original)for(const auto &b:second_original)assert(a.page!=b.page);
  // Themezer's trending order can move between live snapshots. Require a
  // genuinely different page rather than assuming a frozen global ranking.
  unsigned new_themes=0;
  for(const auto &b:second_themezer) {
   bool seen=false;for(const auto &a:themezer)if(a.page==b.page)seen=true;
   if(!seen)++new_themes;
  }
  assert(new_themes>=second_themezer.size()/2);
  assert(catalog_has_next(0,read(argv[5])) && catalog_has_next(1,read(argv[6])));
  std::cout<<"Page 2: "<<second_original.size()<<" original entries (no repeats), "<<second_themezer.size()<<" Themezer entries ("<<new_themes<<" new against prior snapshot)\n";
 }
 if(argc==9) {
  for(int i=0;i<2;++i) {
   auto last=read(argv[7+i]);assert(!parse_catalog(i,last).empty());assert(!catalog_has_next(i,last));
  }
  std::cout<<"Both final catalog pages have themes and correctly report no next page\n";
 }
 std::cout<<"Catalogs: "<<original.size()<<", "<<themezer.size()<<", "<<curated.size()<<"; URL, filename and header checks passed\n";
 for(const auto *v:{&original,&themezer,&curated})std::cout<<v->front().title<<"\t"<<v->front().page<<"\n";
}
