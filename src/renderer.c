#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>
#include "renderer.h"

typedef struct RenWindow {
  SDL_Window *window;
  NVGcontext *vg;
  int w, h;
  float scale;
} RenWindow;

static RenWindow ren = { 0 };

static float get_surface_scale() {
  int w_pixels, h_pixels;
  int w_points, h_points;
  SDL_GL_GetDrawableSize(ren.window, &w_pixels, &h_pixels);
  SDL_GetWindowSize(ren.window, &w_points, &h_points);
  return w_pixels / w_points;
}


void ren_free_window_resources() {
  SDL_DestroyWindow(ren.window);
}


void ren_init(SDL_Window *win) {
  assert(win);
  ren.window = win;
  SDL_GLContext ctx = SDL_GL_CreateContext(win);
  SDL_GL_MakeCurrent(win, ctx);
  ren.vg = nvgCreateGL2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
  ren_resize_window();
}


void ren_resize_window() {
  nvgCancelFrame(ren.vg);
  SDL_GL_GetDrawableSize(ren.window, &ren.w, &ren.h);
  ren.scale = get_surface_scale();
}


void ren_start_frame() {
  glViewport(0, 0, ren.w, ren.h);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  nvgBeginFrame(ren.vg, ren.w, ren.h, ren.scale);
}

void ren_end_frame() {
  static bool initial_frame = true;
  if (initial_frame) {
    SDL_ShowWindow(ren.window);
    initial_frame = false;
  }
  nvgEndFrame(ren.vg);
  SDL_GL_SwapWindow(ren.window);
}

void ren_set_clip_rect(RenRect rect) {
  nvgScissor(ren.vg, rect.x, rect.y, rect.width, rect.height);
}


void ren_get_size(int *x, int *y) {
  *x = ren.w;
  *y = ren.h;
}


int ren_load_font(const char *filename) {
  return nvgCreateFont(ren.vg, filename, filename);
}


void ren_free_font(RenFont *font) {
}


void ren_set_font_size(RenFont *font, float size) {
  nvgFontFaceId(ren.vg, font->handle);
  nvgFontSize(ren.vg, size);
}


void ren_set_font_tab_size(RenFont *font, int n) {
  // fprintf(stderr, "warning: tab size still don't work\n");
}


int ren_get_font_tab_size(RenFont *font) {
  nvgFontFaceId(ren.vg, font->handle);
  return nvgTextBounds(ren.vg, 0, 0, "\t", NULL, NULL);
}


int ren_get_font_width(RenFont *font, const char *text) {
  nvgFontFaceId(ren.vg, font->handle);
  return nvgTextBounds(ren.vg, 0, 0, text, NULL, NULL);
}


int ren_get_font_height(RenFont *font) {
  float lh;
  nvgFontFaceId(ren.vg, font->handle);
  nvgTextMetrics(ren.vg, NULL, NULL, &lh);
  return lh;
}

static void scale_rect(RenRect *rect) {
  rect->x *= ren.scale;
  rect->y *= ren.scale;
  rect->width *= ren.scale;
  rect->height  *= ren.scale;
}

void ren_draw_rect(RenRect rect, NVGcolor color) {
  if (color.a == 0) { return; }

  scale_rect(&rect);
  nvgBeginPath(ren.vg);
  nvgRect(ren.vg, rect.x, rect.y, rect.width, rect.height);
  nvgFillColor(ren.vg, color);
  nvgFill(ren.vg);
}


int ren_draw_text(RenFont *font, const char *text, int x, int y, NVGcolor color) {
  nvgFontFaceId(ren.vg, font->handle);
  nvgFontSize(ren.vg, font->size);
  nvgFillColor(ren.vg, color);
  nvgTextAlign(ren.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
  return nvgText(ren.vg, x, y, text, strstr(text, "\n"));
}
