#ifndef RENBACKEND_H
#define RENBACKEND_H

#include "renderer.h"

typedef struct {
  SDL_Surface *atlas;  /* glyph atlas, one tall surface with glyphs stacked vertically */
  int sy, w, h;        /* source region in the atlas: rows [sy, sy + h), columns [0, w) */
  int dx, dy;          /* destination of the top-left corner */
  bool lcd;            /* atlas holds per-channel (subpixel) coverage */
  RenColor color;
} RenGlyph;

/* A renderer backend. Every window keeps the backend it was created with. */
typedef struct RenBackend {
  const char *name;
  bool (*available)(void);
  SDL_WindowFlags (*window_flags)(void);
  bool (*init_window)(RenWindow *ren);
  void (*free_window)(RenWindow *ren);
  void (*resize)(RenWindow *ren);
  void (*get_size)(RenWindow *ren, int *w, int *h);
  void (*present)(RenWindow *ren, RenRect *rects, int count);
  void (*set_clip)(RenWindow *ren, RenRect rect);
  RenSurface (*get_surface)(RenWindow *ren);
  void (*draw_rect)(RenSurface *rs, RenRect rect, RenColor color);
  void (*draw_glyph)(RenSurface *rs, const RenGlyph *glyph);
} RenBackend;

extern const RenBackend renbackend_soft;

/* software drawing, implemented next to the font code */
void soft_draw_rect(RenSurface *rs, RenRect rect, RenColor color);
void soft_draw_glyph(RenSurface *rs, const RenGlyph *glyph);

#endif
