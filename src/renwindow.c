#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include "renwindow.h"
#include "renbackend.h"

#ifdef LITE_USE_SDL_RENDERER
static int query_surface_scale(RenWindow *ren) {
  int w_pixels, h_pixels;
  int w_points, h_points;
  SDL_GetWindowSizeInPixels(ren->window, &w_pixels, &h_pixels);
  SDL_GetWindowSize(ren->window, &w_points, &h_points);
  /* We consider that the ratio pixel/point will always be an integer and
     it is the same along the x and the y axis. */
  assert(w_pixels % w_points == 0 && h_pixels % h_points == 0 && w_pixels / w_points == h_pixels / h_points);
  return w_pixels / w_points;
}

static void setup_renderer(RenWindow *ren, int w, int h) {
  /* Note that w and h here should always be in pixels and obtained from
     a call to SDL_GetWindowSizeInPixels(). */
  if (!ren->renderer) {
    ren->renderer = SDL_CreateRenderer(ren->window, NULL);
  }
  if (ren->texture) {
    SDL_DestroyTexture(ren->texture);
  }
  ren->texture = SDL_CreateTexture(ren->renderer, ren->rensurface.surface->format, SDL_TEXTUREACCESS_STREAMING, w, h);
  ren->rensurface.scale = query_surface_scale(ren);
}
#endif


void renwin_init_command_buf(RenWindow *ren) {
  ren->command_buf = NULL;
  ren->command_buf_idx = 0;
  ren->command_buf_size = 0;
}

void renwin_show_window(RenWindow *ren) {
  SDL_ShowWindow(ren->window);
}

RenSurface renwin_get_surface(RenWindow *ren) {
  return ren->backend->get_surface(ren);
}


/************************* Software backend *************************/

static RenRect scaled_rect(const RenRect rect, const int scale) {
  return (RenRect) {rect.x * scale, rect.y * scale, rect.width * scale, rect.height * scale};
}

static bool soft_available(void) {
  return true;
}

static SDL_WindowFlags soft_window_flags(void) {
  return SDL_WINDOW_HIGH_PIXEL_DENSITY;
}

static RenSurface soft_get_surface(RenWindow *ren) {
#ifdef LITE_USE_SDL_RENDERER
  RenSurface rs = ren->rensurface;
  rs.backend = ren->backend;
  return rs;
#else
  SDL_Surface *surface = SDL_GetWindowSurface(ren->window);
  if (!surface) {
    fprintf(stderr, "Error getting window surface: %s", SDL_GetError());
    exit(1);
  }
  return (RenSurface){.surface = surface, .scale = 1, .backend = ren->backend};
#endif
}

static bool soft_init_window(RenWindow *ren) {
#ifdef LITE_USE_SDL_RENDERER
  if (ren->rensurface.surface) {
    SDL_DestroySurface(ren->rensurface.surface);
  }
  int w, h;
  SDL_GetWindowSizeInPixels(ren->window, &w, &h);
  SDL_PixelFormat format = SDL_GetWindowPixelFormat(ren->window);
  ren->rensurface.surface = SDL_CreateSurface(w, h, format == SDL_PIXELFORMAT_UNKNOWN ? SDL_PIXELFORMAT_BGRA32 : format);
  if (!ren->rensurface.surface) {
    fprintf(stderr, "Error creating surface: %s", SDL_GetError());
    exit(1);
  }
  setup_renderer(ren, w, h);
#endif
  SDL_SetSurfaceClipRect(soft_get_surface(ren).surface, NULL);
  return true;
}

static void soft_free_window(RenWindow *ren) {
#ifdef LITE_USE_SDL_RENDERER
  SDL_DestroyTexture(ren->texture);
  SDL_DestroyRenderer(ren->renderer);
  SDL_DestroySurface(ren->rensurface.surface);
#else
  (void) ren;
#endif
}

static void soft_resize(RenWindow *ren) {
#ifdef LITE_USE_SDL_RENDERER
  int new_w, new_h, new_scale;
  SDL_GetWindowSizeInPixels(ren->window, &new_w, &new_h);
  new_scale = query_surface_scale(ren);
  /* Note that (w, h) may differ from (new_w, new_h) on retina displays. */
  if (new_scale != ren->rensurface.scale ||
      new_w != ren->rensurface.surface->w ||
      new_h != ren->rensurface.surface->h) {
    soft_init_window(ren);
    setup_renderer(ren, new_w, new_h);
  }
#else
  SDL_Surface *surface = SDL_GetWindowSurface(ren->window);
  int window_w = surface->w, window_h = surface->h;
  SDL_GetWindowSize(ren->window, &window_w, &window_h);
  ren->scale_x = (float)surface->w / window_w;
  ren->scale_y = (float)surface->h / window_h;
#endif
}

static void soft_get_size(RenWindow *ren, int *w, int *h) {
  RenSurface rs = soft_get_surface(ren);
  *w = rs.surface->w;
  *h = rs.surface->h;
#ifdef LITE_USE_SDL_RENDERER
  *w /= rs.scale;
  *h /= rs.scale;
#endif
}

static void soft_present(RenWindow *ren, RenRect *rects, int count) {
#ifdef LITE_USE_SDL_RENDERER
  const int scale = ren->rensurface.scale;
  for (int i = 0; i < count; i++) {
    const RenRect *r = &rects[i];
    const int x = scale * r->x, y = scale * r->y;
    const int w = scale * r->width, h = scale * r->height;
    const SDL_Rect sr = {.x = x, .y = y, .w = w, .h = h};
    uint8_t *pixels = ((uint8_t *) ren->rensurface.surface->pixels) + y * ren->rensurface.surface->pitch + x * SDL_BYTESPERPIXEL(ren->rensurface.surface->format);
    SDL_UpdateTexture(ren->texture, &sr, pixels, ren->rensurface.surface->pitch);
  }
  SDL_RenderTexture(ren->renderer, ren->texture, NULL, NULL);
  SDL_RenderPresent(ren->renderer);
#else
  SDL_UpdateWindowSurfaceRects(ren->window, (SDL_Rect*) rects, count);
#endif
}

static void soft_set_clip(RenWindow *ren, RenRect rect) {
  RenSurface rs = soft_get_surface(ren);
  RenRect sr = scaled_rect(rect, rs.scale);
  SDL_SetSurfaceClipRect(rs.surface, &(SDL_Rect){.x = sr.x, .y = sr.y, .w = sr.width, .h = sr.height});
}

const RenBackend renbackend_soft = {
  .name = "soft",
  .available = soft_available,
  .window_flags = soft_window_flags,
  .init_window = soft_init_window,
  .free_window = soft_free_window,
  .resize = soft_resize,
  .get_size = soft_get_size,
  .present = soft_present,
  .set_clip = soft_set_clip,
  .get_surface = soft_get_surface,
  .draw_rect = soft_draw_rect,
  .draw_glyph = soft_draw_glyph,
};
