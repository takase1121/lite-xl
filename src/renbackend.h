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

typedef enum { BACKEND_FLAG_FULL_REDRAW = 1 } ERenBackendFlag;

/* properties handed to available() by ren_set_backend */
/* string: GL profiles to try, in order. "gles", "gl" or "gles,gl"; a single entry has no fallback */
#define RENBACKEND_PROP_GL_BACKEND "gl_backend"

/* A renderer backend. Every window keeps the backend it was created with. */
typedef struct RenBackend {
  const char *name;
  uint32_t flags;
  bool (*available)(SDL_PropertiesID props);
  SDL_WindowFlags (*window_flags)(void);
  bool (*init_window)(RenWindow *ren);
  void (*free_window)(RenWindow *ren);
  void (*resize)(RenWindow *ren);
  void (*get_size)(RenWindow *ren, int *w, int *h);
  RenSurface (*get_surface)(RenWindow *ren);
  void (*begin_frame)(RenWindow *ren);
  void (*end_frame)(RenWindow *ren, RenRect *rects, int count);
  void (*set_clip)(RenWindow *ren, RenRect rect);
  void (*draw_rect)(RenSurface *rs, RenRect rect, RenColor color);
  void (*draw_glyph)(RenSurface *rs, const RenGlyph *glyph);
  bool (*atlas_create)(RenAtlas *atlas);
  void (*atlas_destroy)(RenAtlas *atlas);
  void (*atlas_upload)(RenAtlas *atlas, int y, int rows, const uint8_t *src, int pitch, int row_bytes);
  int (*max_atlas_size)(void);
} RenBackend;

extern const RenBackend renbackend_soft;
extern const RenBackend renbackend_gl;

#endif
