#include "launcher/font_text.h"
#include "launcher/font_8x13.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Printable ASCII only: 0x20..0x7E. */
#define A4_GLYPH_FIRST 0x20
#define A4_GLYPH_COUNT 95
#define A4_ATLAS_COLS 16
#define A4_ATLAS_ROWS 6

struct A4Font {
  SDL_Texture* tex;
  int index[A4_GLYPH_COUNT]; /* font_rows index per ASCII code */
};

/* font_cp is sorted; binary search. Returns -1 when absent. */
static int find_cp(uint32_t cp) {
  int lo = 0, hi = FONT_GLYPHS - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (font_cp[mid] == cp) return mid;
    if (font_cp[mid] < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return -1;
}

struct A4Font* a4_font_create(SDL_Renderer* ren) {
  if (!ren) return NULL;
  struct A4Font* f = (struct A4Font*)calloc(1, sizeof *f);
  if (!f) return NULL;
  int q = find_cp(0x3F); /* '?' fallback */
  if (q < 0) q = 0;
  for (int i = 0; i < A4_GLYPH_COUNT; i++) {
    int idx = find_cp((uint32_t)(A4_GLYPH_FIRST + i));
    f->index[i] = idx >= 0 ? idx : q;
  }

  int w = A4_ATLAS_COLS * FONT_W, h = A4_ATLAS_ROWS * FONT_H;
  uint32_t* px = (uint32_t*)calloc((size_t)w * h, sizeof(uint32_t));
  if (!px) {
    free(f);
    return NULL;
  }
  for (int i = 0; i < A4_GLYPH_COUNT; i++) {
    int cx = (i % A4_ATLAS_COLS) * FONT_W;
    int cy = (i / A4_ATLAS_COLS) * FONT_H;
    const uint8_t* rows = font_rows[f->index[i]];
    for (int y = 0; y < FONT_H; y++) {
      uint8_t bits = rows[y];
      for (int x = 0; x < FONT_W; x++) {
        if (bits & (0x80u >> x))
          px[(size_t)(cy + y) * w + (cx + x)] = 0xFFFFFFFFu;
      }
    }
  }
  f->tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888,
                             SDL_TEXTUREACCESS_STATIC, w, h);
  if (!f->tex) {
    free(px);
    free(f);
    return NULL;
  }
  SDL_UpdateTexture(f->tex, NULL, px, w * 4);
  SDL_SetTextureBlendMode(f->tex, SDL_BLENDMODE_BLEND);
  free(px);
  return f;
}

void a4_font_destroy(struct A4Font* font) {
  if (!font) return;
  if (font->tex) SDL_DestroyTexture(font->tex);
  free(font);
}

int a4_font_probe(struct A4Font* font, SDL_Renderer* ren, int x, int y) {
  if (!font || !font->tex || !ren) return -1;
  int slot = 0x41 - A4_GLYPH_FIRST; /* 'A' */
  SDL_Rect src = { (slot % A4_ATLAS_COLS) * FONT_W,
                   (slot / A4_ATLAS_COLS) * FONT_H, FONT_W, FONT_H };
  SDL_Rect dst = { x, y, FONT_W, FONT_H };
  return SDL_RenderCopy(ren, font->tex, &src, &dst);
}

void a4_draw_text(struct A4Font* font, SDL_Renderer* ren, int x, int y,
                  const char* text, int scale,
                  unsigned char r, unsigned char g, unsigned char b) {
  if (!font || !font->tex || !ren || !text || scale < 1) return;
  SDL_SetTextureColorMod(font->tex, r, g, b);
  int cx = x;
  for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
    unsigned char c = *p;
    int slot = (c >= A4_GLYPH_FIRST && c < A4_GLYPH_FIRST + A4_GLYPH_COUNT)
                 ? (c - A4_GLYPH_FIRST)
                 : (0x3F - A4_GLYPH_FIRST);
    SDL_Rect src = { (slot % A4_ATLAS_COLS) * FONT_W,
                     (slot / A4_ATLAS_COLS) * FONT_H, FONT_W, FONT_H };
    SDL_Rect dst = { cx, y, FONT_W * scale, FONT_H * scale };
    SDL_RenderCopy(ren, font->tex, &src, &dst);
    cx += FONT_W * scale;
  }
}

int a4_text_width(const char* text, int scale) {
  if (!text || scale < 1) return 0;
  return (int)strlen(text) * FONT_W * scale;
}
