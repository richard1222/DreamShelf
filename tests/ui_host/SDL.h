#pragma once
#include <cstdint>
#include <vector>
struct SDL_Renderer { std::vector<unsigned char> pixels=std::vector<unsigned char>(1280*720*3);unsigned char r=0,g=0,b=0; };
struct SDL_Rect {int x,y,w,h;};
struct SDL_Point {int x,y;};
int SDL_SetRenderDrawColor(SDL_Renderer*,unsigned char,unsigned char,unsigned char,unsigned char);
int SDL_RenderClear(SDL_Renderer*);
int SDL_RenderFillRect(SDL_Renderer*,const SDL_Rect*);
int SDL_RenderDrawLine(SDL_Renderer*,int,int,int,int);
int SDL_RenderDrawLines(SDL_Renderer*,const SDL_Point*,int);
