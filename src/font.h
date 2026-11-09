#ifndef FONT_H
#define FONT_H

#include <SDL3/SDL.h>
#include "renderer.h"

// number of subpixel bitmaps
#define SUBPIXEL_BITMAPS_CACHED 3

typedef struct {
  RenFont *font;
  RenAtlas *atlas;      // NULL when the glyph has no bitmap
  int sy, w, h;        // bitmap region in the atlas
  int left, top;       // bitmap offset from the pen position and baseline
  bool lcd;            // atlas holds per-channel coverage
  float xadvance;
  float space_advance;
  int font_height, underline_thickness; // of the font the glyph came from
} FontGlyph;

int font_init(void);
void font_free(void);
const char* utf8_to_codepoint(const char *p, const char *endp, unsigned *dst);
int font_group_get_baseline(RenFont **fonts);
unsigned char font_group_get_style(RenFont **fonts);
bool font_group_lookup(RenFont **fonts, unsigned int codepoint, int subpixel_idx, FontGlyph *out);
float font_get_xadvance(RenFont *font, unsigned int codepoint, float xadvance, double curr_x, RenTab tab);

// implemented by the renderer, forwarding to the backends
RenAtlas *ren_atlas_create(int w, int h, SDL_PixelFormat format);
void ren_atlas_upload(RenAtlas *atlas, int y, int rows, const uint8_t *src, int pitch, int row_bytes);
void ren_atlas_destroy(RenAtlas *atlas);
int ren_max_atlas_size(void);

// https://en.wikipedia.org/wiki/Whitespace_character
static inline int is_whitespace(unsigned int codepoint) {
  switch (codepoint) {
    case 0x20: case 0x85: case 0xA0: case 0x1680: case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000: return 1;
  }
  return (codepoint >= 0x9 && codepoint <= 0xD) || (codepoint >= 0x2000 && codepoint <= 0x200A);
}

#endif
