#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "renderer.h"
#include "renwindow.h"
#include "renbackend.h"
#include "font.h"

static RenWindow **window_list = NULL;
static RenWindow *target_window = NULL;
static size_t window_count = 0;

static const RenBackend *backend = &renbackend_soft;
static const RenBackend *const backends[] = { &renbackend_soft, &renbackend_gl };

double ren_draw_text(RenSurface *rs, RenFont **fonts, const char *text, size_t len, float x, int y, RenColor color, RenTab tab) {
  const int surface_scale = rs->scale;
  double pen_x = x * surface_scale;
  double original_pen_x = pen_x;
  y *= surface_scale;
  const char* end = text + len;

  const int baseline = font_group_get_baseline(fonts) * surface_scale;
  RenFont* last = NULL;
  int last_height = 0, last_thickness = 0;
  double last_pen_x = x;
  bool underline = font_group_get_style(fonts) & FONT_STYLE_UNDERLINE;
  bool strikethrough = font_group_get_style(fonts) & FONT_STYLE_STRIKETHROUGH;

  while (text < end) {
    unsigned int codepoint;
    text = utf8_to_codepoint(text, end,  &codepoint);
    FontGlyph glyph;
    if (!font_group_lookup(fonts, codepoint, (int)(fmod(pen_x, 1.0) * SUBPIXEL_BITMAPS_CACHED), &glyph))
      break;
    int start_x = floor(pen_x) + glyph.left;
    if (!glyph.atlas && !is_whitespace(codepoint))
      ren_draw_rect(rs, (RenRect){ start_x + 1, y, glyph.space_advance - 1, ren_font_group_get_height(fonts) }, color);
    if (!is_whitespace(codepoint) && glyph.atlas && color.a > 0) {
      rs->backend->draw_glyph(rs, &(RenGlyph){
        .atlas = glyph.atlas,
        .sy = glyph.sy, .w = glyph.w, .h = glyph.h,
        .dx = start_x, .dy = y - glyph.top + baseline,
        .lcd = glyph.lcd,
        .color = color,
      });
    }

    float adv = font_get_xadvance(fonts[0], codepoint, glyph.xadvance, pen_x - original_pen_x, tab);

    if(!last) {
      last = glyph.font;
      last_height = glyph.font_height;
      last_thickness = glyph.underline_thickness;
    } else if(glyph.font != last || text == end) {
      double local_pen_x = text == end ? pen_x + adv : pen_x;
      if (underline)
        ren_draw_rect(rs, (RenRect){last_pen_x, y / surface_scale + last_height - 1, (local_pen_x - last_pen_x) / surface_scale, last_thickness * surface_scale}, color);
      if (strikethrough)
        ren_draw_rect(rs, (RenRect){last_pen_x, y / surface_scale + last_height / 2, (local_pen_x - last_pen_x) / surface_scale, last_thickness * surface_scale}, color);
      last = glyph.font;
      last_height = glyph.font_height;
      last_thickness = glyph.underline_thickness;
      last_pen_x = pen_x;
    }

    pen_x += adv;
  }
  return pen_x / surface_scale;
}

/******************* Rectangles **********************/
static inline RenColor blend_pixel(RenColor dst, RenColor src) {
  int ia = 0xff - src.a;
  dst.r = ((src.r * src.a) + (dst.r * ia)) >> 8;
  dst.g = ((src.g * src.a) + (dst.g * ia)) >> 8;
  dst.b = ((src.b * src.a) + (dst.b * ia)) >> 8;
  return dst;
}

void ren_draw_rect(RenSurface *rs, RenRect rect, RenColor color) {
  if (color.a == 0) { return; }
  rs->backend->draw_rect(rs, rect, color);
}

/*************** Window Management ****************/
static void ren_add_window(RenWindow *window_renderer) {
  window_count += 1;
  window_list = SDL_realloc(window_list, window_count * sizeof(RenWindow*));
  window_list[window_count-1] = window_renderer;
}

static void ren_remove_window(RenWindow *window_renderer) {
  for (size_t i = 0; i < window_count; ++i) {
    if (window_list[i] == window_renderer) {
      window_count -= 1;
      memmove(&window_list[i], &window_list[i+1], (window_count - i) * sizeof(RenWindow*));
      return;
    }
  }
}

int video_init(void) {
  static int ren_inited = 0;
  if (!ren_inited) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
      return -1;     
    SDL_EnableScreenSaver();
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "1");
    /* This hint tells SDL to respect borderless window as a normal window.
    ** For example, the window will sit right on top of the taskbar instead
    ** of obscuring it. */
    SDL_SetHint("SDL_BORDERLESS_WINDOWED_STYLE", "1");
    /* This hint tells SDL to allow the user to resize a borderless windoow.
    ** It also enables aero-snap on Windows apparently. */
    SDL_SetHint("SDL_BORDERLESS_RESIZABLE_STYLE", "1");
    SDL_SetHint("SDL_MOUSE_DOUBLE_CLICK_RADIUS", "4");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    if (SDL_getenv("LITE_GL") && !ren_set_backend("gl"))
      fprintf(stderr, "renderer: LITE_GL set but the GL backend is not built in (no glesv2 or LITE_USE_SDL_RENDERER)\n");
    fprintf(stderr, "renderer: %s backend\n", backend->name);
    ren_inited = 1;
  }
  return 0;
}

// selects the backend used by windows created from now on
bool ren_set_backend(const char *name) {
  for (size_t i = 0; i < sizeof(backends) / sizeof(*backends); i++) {
    if (strcmp(backends[i]->name, name) == 0 && backends[i]->available()) {
      backend = backends[i];
      return true;
    }
  }
  return false;
}

RenAtlas *ren_atlas_create(int w, int h, SDL_PixelFormat format) {
  RenAtlas *atlas = SDL_calloc(1, sizeof(RenAtlas));
  if (!atlas) return NULL;
  atlas->backend = backend;
  atlas->w = w;
  atlas->h = h;
  atlas->format = format;
  if (!backend->atlas_create(atlas)) {
    SDL_free(atlas);
    return NULL;
  }
  return atlas;
}

void ren_atlas_upload(RenAtlas *atlas, int y, int rows, const uint8_t *src, int pitch, int row_bytes) {
  atlas->backend->atlas_upload(atlas, y, rows, src, pitch, row_bytes);
}

void ren_atlas_destroy(RenAtlas *atlas) {
  atlas->backend->atlas_destroy(atlas);
  SDL_free(atlas);
}

int ren_max_atlas_size(void) {
  return backend->max_atlas_size ? backend->max_atlas_size() : 0;
}

SDL_WindowFlags ren_window_flags(void) {
  return backend->window_flags();
}

int ren_init(void) {
  return font_init();
}

void ren_free(void) {
  font_free();
}

RenWindow* ren_create(SDL_Window *win) {
  assert(win);
  RenWindow* window_renderer = SDL_calloc(1, sizeof(RenWindow));

  window_renderer->window = win;
  window_renderer->backend = backend;
  window_renderer->scale_x = window_renderer->scale_y = 1;
  if (!backend->init_window(window_renderer)) {
    fprintf(stderr, "Error creating %s renderer: %s\n", backend->name, SDL_GetError());
    exit(EXIT_FAILURE);
  }
  renwin_init_command_buf(window_renderer);

  ren_add_window(window_renderer);
  return window_renderer;
}

void ren_destroy(RenWindow* window_renderer) {
  assert(window_renderer);
  ren_remove_window(window_renderer);
  window_renderer->backend->free_window(window_renderer);
  SDL_DestroyWindow(window_renderer->window);
  window_renderer->window = NULL;
  SDL_free(window_renderer->command_buf);
  window_renderer->command_buf = NULL;
  window_renderer->command_buf_size = 0;
  SDL_free(window_renderer);
}

void ren_resize_window(RenWindow *window_renderer) {
  window_renderer->backend->resize(window_renderer);
}

// TODO: Does not work nicely with multiple windows
void ren_update_rects(RenWindow *window_renderer, RenRect *rects, int count) {
  static bool initial_frame = true;
  window_renderer->backend->end_frame(window_renderer, rects, count);
  if (initial_frame) {
    renwin_show_window(window_renderer);
    initial_frame = false;
  }
}


void ren_set_clip_rect(RenWindow *window_renderer, RenRect rect) {
  window_renderer->backend->set_clip(window_renderer, rect);
}


void ren_get_size(RenWindow *window_renderer, int *x, int *y) {
  window_renderer->backend->get_size(window_renderer, x, y);
}

size_t ren_get_window_list(RenWindow ***window_list_dest) {
  *window_list_dest = window_list;
  return window_count;
}

RenWindow* ren_find_window(SDL_Window *window) {
  for (size_t i = 0; i < window_count; ++i) {
    RenWindow* window_renderer = window_list[i];
    if (window_renderer->window == window) {
      return window_renderer;
    }
  }

  return NULL;
}

RenWindow* ren_find_window_from_id(uint32_t id) {
  SDL_Window *window = SDL_GetWindowFromID(id);
  return ren_find_window(window);
}

RenWindow* ren_get_target_window(void)
{
  return target_window;
}

void ren_set_target_window(RenWindow *window)
{
  target_window = window;
}
