// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace {
struct Color { unsigned char r,g,b; };
const Color ink={45,49,54},muted={105,112,119},orange={213,72,8},paper={255,255,255};
void color(SDL_Renderer *r,Color c){SDL_SetRenderDrawColor(r,c.r,c.g,c.b,255);}
void box(SDL_Renderer *r,int x,int y,int w,int h,Color c){color(r,c);SDL_Rect rect={x,y,w,h};SDL_RenderFillRect(r,&rect);}
void rounded(SDL_Renderer *r,int x,int y,int w,int h,int rad,Color c) {
 color(r,c);
 SDL_Rect center={x,y+rad,w,h-2*rad};SDL_RenderFillRect(r,&center);
 for(int row=0;row<rad;++row) {
  int dy=rad-row-1;
  int inset=dy?(int)std::ceil(rad-std::sqrt((double)rad*rad-(double)dy*dy)):0;
  SDL_RenderDrawLine(r,x+inset,y+row,x+w-inset-1,y+row);
  SDL_RenderDrawLine(r,x+inset,y+h-row-1,x+w-inset-1,y+h-row-1);
 }
}
void text(SDL_Renderer *r,A4Font *f,int x,int y,const std::string &s,int scale,Color c) {
 a4_draw_text(f,r,x,y,s.c_str(),scale,c.r,c.g,c.b);
}
std::string clipped(const std::string &s,size_t count) {
 return s.size()>count?s.substr(0,count-3)+"...":s;
}
void spiral(SDL_Renderer *r,int x,int y) {
 color(r,orange);SDL_Point points[181];
 for(int i=0;i<=180;++i) {
  double a=i*0.075,radius=2+i*0.15;
  int nx=x+(int)(std::cos(a)*radius),ny=y+(int)(std::sin(a)*radius);
  points[i]={nx,ny};
 }
 for(int thick=-2;thick<=2;++thick) {
  SDL_Point shifted[181];for(int i=0;i<=180;++i)shifted[i]={points[i].x,points[i].y+thick};
  SDL_RenderDrawLines(r,shifted,181);
 }
}
void key(SDL_Renderer *r,A4Font *f,int x,int y,const std::string &button,const std::string &label) {
 int width=(int)button.size()*8+16;rounded(r,x,y,width,24,5,ink);
 text(r,f,x+8,y+5,button,1,paper);text(r,f,x+width+9,y-1,label,2,ink);
}
}
void draw_theme_ui(SDL_Renderer *r,A4Font *f,int source,unsigned page,
 const std::vector<Theme> &themes,int selected,const std::string &message,bool busy,unsigned received) {
 color(r,{239,241,242});SDL_RenderClear(r);
 box(r,0,0,1280,8,orange);spiral(r,91,72);
 text(r,f,140,43,"DreamShelf",4,ink);
 text(r,f,143,103,"YOUR PS3 THEME BROWSER",1,muted);
 text(r,f,936,48,"ONLINE CATALOG",2,muted);
 text(r,f,1048,88,"PAGE "+std::to_string(page),2,orange);
 rounded(r,43,155,260,410,14,{217,222,225});rounded(r,40,150,260,410,14,paper);
 rounded(r,327,155,916,410,14,{217,222,225});rounded(r,324,150,916,410,14,paper);
 text(r,f,62,175,"CATALOGS",2,muted);
 for(int i=0;i<3;++i) {
  int y=218+i*61;bool active=i==source;
  rounded(r,54,y,232,49,8,active?orange:Color{241,243,244});
  text(r,f,67,y+12,i==2?"Backup picks":sources[i].name,2,active?paper:ink);
 }
 // A small VMU-inspired LCD shows real transfer state, without idle animation.
 rounded(r,88,419,162,125,12,{217,222,225});rounded(r,92,423,154,117,10,{245,245,243});
 text(r,f,139,432,"VMU",1,muted);box(r,103,451,132,73,{67,83,75});box(r,107,455,124,65,{173,193,170});
 Color lcd={48,70,52};text(r,f,115,464,busy?"CONNECTING":"CATALOG READY",1,lcd);
 char amount[24];snprintf(amount,sizeof amount,"%u KB",received/1024);
 text(r,f,115,486,busy?amount:std::to_string(themes.size())+" THEMES",1,lcd);
 text(r,f,348,172,sources[source].name,2,ink);
 text(r,f,348,204,clipped(catalog_page_url(source,page),105),1,muted);
 int first=(selected/8)*8;
 for(int row=0;row<8 && first+row<(int)themes.size();++row) {
  int idx=first+row,y=229+row*38;bool active=idx==selected;
  rounded(r,340,y,884,33,6,active?orange:Color{246,247,248});
  Color fg=active?paper:ink;
  box(r,353,y+6,23,21,active?Color{242,173,123}:Color{216,224,229});
  text(r,f,356,y+11,"P3",1,active?ink:muted);
  text(r,f,388,y+3,clipped(themes[idx].title,48),2,fg);
  text(r,f,1185,y+10,std::to_string(idx+1),1,fg);
 }
 if(themes.empty()) {
  text(r,f,370,308,busy?"Fetching themes...":"No catalog loaded.",3,muted);
  text(r,f,371,367,"L1/R1 chooses another site. Triangle retries.",2,muted);
 }
 rounded(r,40,580,1200,72,10,paper);box(r,40,594,5,44,orange);
 std::string first_line=message,second_line;
 if(first_line.size()>70) {
  size_t cut=first_line.rfind(' ',70);if(cut==std::string::npos || cut<35)cut=70;
  second_line=first_line.substr(cut+(first_line[cut]==' '?1:0));first_line.resize(cut);
 }
 text(r,f,61,591,first_line,2,ink);text(r,f,61,623,clipped(second_line,70),1,muted);
 key(r,f,48,675,"X","Download");key(r,f,246,675,"L1/R1","Site");
 key(r,f,440,675,"L2/R2","Page");key(r,f,635,675,"Triangle","Refresh");key(r,f,920,675,"Circle","Back / Quit");
}
