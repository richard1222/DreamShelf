#ifndef A4_FONT_TEXT_H
#define A4_FONT_TEXT_H

#include "SDL.h"

/* Bitmap text for the M0 menu: the X11 "fixed" 8x13 glyphs (public domain
 * font, table vendored in launcher/font_8x13.h) uploaded once into an
 * atlas texture and drawn with SDL_RenderCopy. No external font files,
 * no TTF dependency. */

struct A4Font;

/* Build the atlas for renderer ren. Returns NULL on failure. */
struct A4Font* a4_font_create(SDL_Renderer* ren);
void a4_font_destroy(struct A4Font* font);

/* Diagnostic: copy a single atlas glyph ('A', scale 1) at (x, y) and
 * return the SDL_RenderCopy result. Used before the menu loop to prove
 * (or implicate) the texture path separately from SDL_RenderPresent. */
int a4_font_probe(struct A4Font* font, SDL_Renderer* ren, int x, int y);

/* Draw text. Bytes outside printable ASCII render as '?'. */
void a4_draw_text(struct A4Font* font, SDL_Renderer* ren, int x, int y,
                  const char* text, int scale,
                  unsigned char r, unsigned char g, unsigned char b);
int a4_text_width(const char* text, int scale);

#endif
