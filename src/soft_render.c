#include <stdio.h>
#include <stdlib.h>
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

// 1x1 surface used to blend translucent rectangles
static SDL_Surface *draw_rect_surface = NULL;

static RenRect scaled_rect(const RenRect rect, const int scale) {
  return (RenRect) {rect.x * scale, rect.y * scale, rect.width * scale, rect.height * scale};
}

static bool soft_available(SDL_PropertiesID props) {
  (void) props;
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

static void soft_begin_frame(RenWindow *ren) { (void) ren; }

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

static void soft_draw_glyph(RenSurface *rs, const RenGlyph *g) {
  SDL_Surface *surface = rs->surface;
  SDL_Rect clip;
  SDL_GetSurfaceClipRect(surface, &clip);
  int clip_end_x = clip.x + clip.w, clip_end_y = clip.y + clip.h;
  int start_x = g->dx;
  if (start_x + g->w < clip.x || start_x >= clip_end_x)
    return;

  uint8_t *destination_pixels = surface->pixels;
  SDL_Surface *atlas = g->atlas->surface;
  uint8_t *source_pixels = atlas->pixels;
  int glyph_end = g->w, glyph_start = 0;
  for (int line = 0; line < g->h; ++line) {
    int target_y = line + g->dy;
    if (target_y < clip.y)
      continue;
    if (target_y >= clip_end_y)
      break;
    if (start_x + (glyph_end - glyph_start) >= clip_end_x)
      glyph_end = glyph_start + (clip_end_x - start_x);
    if (start_x < clip.x) {
      int offset = clip.x - start_x;
      start_x += offset;
      glyph_start += offset;
    }

    const SDL_PixelFormatDetails* surface_format = SDL_GetPixelFormatDetails(surface->format);
    const SDL_PixelFormatDetails* font_surface_format = SDL_GetPixelFormatDetails(atlas->format);

    uint32_t* destination_pixel = (uint32_t*)&(destination_pixels[surface->pitch * target_y + start_x * surface_format->bytes_per_pixel]);
    uint8_t* source_pixel = &source_pixels[(g->sy + line) * atlas->pitch + glyph_start * font_surface_format->bytes_per_pixel];
    for (int x = glyph_start; x < glyph_end; ++x) {
      uint32_t destination_color = *destination_pixel;
      // the standard way of doing this would be SDL_GetRGBA, but that introduces a performance regression. needs to be investigated
      SDL_Color dst = {
        (destination_color & surface_format->Rmask) >> surface_format->Rshift,
        (destination_color & surface_format->Gmask) >> surface_format->Gshift,
        (destination_color & surface_format->Bmask) >> surface_format->Bshift,
        (destination_color & surface_format->Amask) >> surface_format->Ashift};
      SDL_Color src;

      if (g->lcd) {
        src.r = *(source_pixel++);
        src.g = *(source_pixel++);
      } else {
        src.r = *(source_pixel);
        src.g = *(source_pixel);
      }

      src.b = *(source_pixel++);
      src.a = 0xFF;

      unsigned r = (g->color.r * src.r * g->color.a + dst.r * (65025 - src.r * g->color.a) + 32767) / 65025;
      unsigned gr = (g->color.g * src.g * g->color.a + dst.g * (65025 - src.g * g->color.a) + 32767) / 65025;
      unsigned b = (g->color.b * src.b * g->color.a + dst.b * (65025 - src.b * g->color.a) + 32767) / 65025;
      // the standard way of doing this would be SDL_GetRGBA, but that introduces a performance regression. needs to be investigated
      *destination_pixel++ = (unsigned int) dst.a << surface_format->Ashift | r << surface_format->Rshift | gr << surface_format->Gshift | b << surface_format->Bshift;
    }
  }
}

static void soft_draw_rect(RenSurface *rs, RenRect rect, RenColor color) {
  SDL_Surface *surface = rs->surface;
  const int surface_scale = rs->scale;

  SDL_Rect dest_rect = { rect.x * surface_scale,
                         rect.y * surface_scale,
                         rect.width * surface_scale,
                         rect.height * surface_scale };

  if (color.a == 0xff) {
    uint32_t translated = SDL_MapSurfaceRGB(surface, color.r, color.g, color.b);
    SDL_FillSurfaceRect(surface, &dest_rect, translated);
  } else {
    // Seems like SDL doesn't handle clipping as we expect when using
    // scaled blitting, so we "clip" manually.
    SDL_Rect clip;
    SDL_GetSurfaceClipRect(surface, &clip);
    if (!SDL_GetRectIntersection(&clip, &dest_rect, &dest_rect)) return;

    if (!draw_rect_surface) {
      draw_rect_surface = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32);
      if (!draw_rect_surface) return;
    }
    uint32_t *pixel = (uint32_t *)draw_rect_surface->pixels;
    *pixel = SDL_MapSurfaceRGBA(draw_rect_surface, color.r, color.g, color.b, color.a);
    SDL_BlitSurfaceScaled(draw_rect_surface, NULL, surface, &dest_rect, SDL_SCALEMODE_LINEAR);
  }
}

static bool soft_atlas_create(RenAtlas *atlas) {
  atlas->surface = SDL_CreateSurface(atlas->w, atlas->h, atlas->format);
  return atlas->surface != NULL;
}

static void soft_atlas_destroy(RenAtlas *atlas) {
  SDL_DestroySurface(atlas->surface);
}

static void soft_atlas_upload(RenAtlas *atlas, int y, int rows, const uint8_t *src, int pitch, int row_bytes) {
  SDL_Surface *surface = atlas->surface;
  for (int line = 0; line < rows; line++) {
    SDL_memcpy((uint8_t*) surface->pixels + (size_t) surface->pitch * (y + line), src + (size_t) pitch * line, row_bytes);
  }
}

const RenBackend renbackend_soft = {
  .name = "soft",
  .flags = 0,
  .available = soft_available,
  .window_flags = soft_window_flags,
  .init_window = soft_init_window,
  .free_window = soft_free_window,
  .resize = soft_resize,
  .get_size = soft_get_size,
  .begin_frame = soft_begin_frame,
  .end_frame = soft_present,
  .set_clip = soft_set_clip,
  .get_surface = soft_get_surface,
  .draw_rect = soft_draw_rect,
  .draw_glyph = soft_draw_glyph,
  .atlas_create = soft_atlas_create,
  .atlas_destroy = soft_atlas_destroy,
  .atlas_upload = soft_atlas_upload,
};
