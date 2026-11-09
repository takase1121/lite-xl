#ifndef RENBACKEND_H
#define RENBACKEND_H

#include "renderer.h"

typedef struct {
  RenAtlas *atlas;     /* glyph atlas, one tall surface with glyphs stacked vertically */
  int sy, w, h;        /* source region in the atlas: rows [sy, sy + h), columns [0, w) */
  int dx, dy;          /* destination of the top-left corner */
  bool lcd;            /* atlas holds per-channel (subpixel) coverage */
  RenColor color;
} RenGlyph;

/* A renderer backend. Every window keeps the backend it was created with. */
typedef struct RenBackend {
  const char *name;
  bool full_redraw; /* redraws the whole frame each time, no dirty rectangles */
  bool (*available)(void);
  SDL_WindowFlags (*window_flags)(void);
  bool (*init_window)(RenWindow *ren);
  void (*free_window)(RenWindow *ren);
  void (*resize)(RenWindow *ren);
  void (*get_size)(RenWindow *ren, int *w, int *h);
  void (*begin_frame)(RenWindow *ren);
  void (*present)(RenWindow *ren, RenRect *rects, int count);
  void (*set_clip)(RenWindow *ren, RenRect rect);
  RenSurface (*get_surface)(RenWindow *ren);
  void (*draw_rect)(RenSurface *rs, RenRect rect, RenColor color);
  void (*draw_glyph)(RenSurface *rs, const RenGlyph *glyph);
  /* The renderer allocates the RenAtlas and fills w, h and format; the backend owns ->surface. */
  bool (*atlas_create)(RenAtlas *atlas);
  void (*atlas_destroy)(RenAtlas *atlas);
  /* copies `rows` rows of `row_bytes` bytes at `src` into the atlas starting at row y */
  void (*atlas_upload)(RenAtlas *atlas, int y, int rows, const uint8_t *src, int pitch, int row_bytes);
  /* optional */
  int (*max_atlas_size)(void);
} RenBackend;

extern const RenBackend renbackend_soft;
extern const RenBackend renbackend_gl;

#endif
