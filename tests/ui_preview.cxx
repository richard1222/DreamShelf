// Host rasterization of the production UI using its actual bitmap glyphs.
#include "ui.h"
#include "launcher/font_8x13.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
static unsigned line_calls=0;
int SDL_SetRenderDrawColor(SDL_Renderer*r,unsigned char red,unsigned char green,unsigned char blue,unsigned char){r->r=red;r->g=green;r->b=blue;return 0;}
static void pixel(SDL_Renderer*r,int x,int y){if(x<0||x>=1280||y<0||y>=720)return;auto i=(y*1280+x)*3;r->pixels[i]=r->r;r->pixels[i+1]=r->g;r->pixels[i+2]=r->b;}
int SDL_RenderClear(SDL_Renderer*r){for(int y=0;y<720;++y)for(int x=0;x<1280;++x)pixel(r,x,y);return 0;}
int SDL_RenderFillRect(SDL_Renderer*r,const SDL_Rect*q){for(int y=q->y;y<q->y+q->h;++y)for(int x=q->x;x<q->x+q->w;++x)pixel(r,x,y);return 0;}
int SDL_RenderDrawLine(SDL_Renderer*r,int x,int y,int x2,int y2){++line_calls;int dx=std::abs(x2-x),sx=x<x2?1:-1,dy=-std::abs(y2-y),sy=y<y2?1:-1,e=dx+dy;for(;;){pixel(r,x,y);if(x==x2&&y==y2)break;int e2=2*e;if(e2>=dy){e+=dy;x+=sx;}if(e2<=dx){e+=dx;y+=sy;}}return 0;}
void a4_draw_text(A4Font*,SDL_Renderer*r,int x,int y,const char*s,int scale,unsigned char red,unsigned char green,unsigned char blue){
 SDL_SetRenderDrawColor(r,red,green,blue,255);
 for(;*s;++s,x+=FONT_W*scale){int glyph=-1;for(int i=0;i<FONT_GLYPHS;++i)if(font_cp[i]==(unsigned char)*s){glyph=i;break;}if(glyph<0)continue;
 for(int row=0;row<FONT_H;++row)for(int col=0;col<FONT_W;++col)if(font_rows[glyph][row]&(0x80>>col)){SDL_Rect q={x+col*scale,y+row*scale,scale,scale};SDL_RenderFillRect(r,&q);}}
}
int SDL_RenderDrawLines(SDL_Renderer*r,const SDL_Point*p,int n){++line_calls;for(int i=1;i<n;++i){SDL_RenderDrawLine(r,p[i-1].x,p[i-1].y,p[i].x,p[i].y);--line_calls;}return 0;}
int main(int argc,char**argv){if(argc!=2)return 1;SDL_Renderer r;
 std::vector<Theme> t={{"Jujutsu Kaisen (Slideshow)","",""},{"Genshin Impact - Slideshow Theme","",""},{"Wonder Woman Movie 2017 Slideshow","",""},{"Sword Art Online - Kirito and Asuna","",""},{"Kaguya-sama: Love Is War","",""},{"Mortal Kombat 9 Girls Slideshow","",""},{"Final Fantasy VII Remake","",""},{"Hatsune Miku Animated (Snow Effect)","",""}};
 draw_theme_ui(&r,nullptr,0,2,t,2,"Choose a theme. X downloads; L1/R1 changes source.",false,0);
 std::ofstream f(argv[1],std::ios::binary);f<<"P6\n1280 720\n255\n";f.write((char*)r.pixels.data(),r.pixels.size());std::cout<<"UI line API calls: "<<line_calls<<"\n";return f?0:1;
}
