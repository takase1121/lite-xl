#ifndef RENDERER_H
#define RENDERER_H

#include <SDL.h>
#include <GL/gl.h>

#include <stdint.h>

#define NANOVG_GL2
#include "nanovg.h"
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"

typedef struct {
  int handle;
  float size;
  float height;
} RenFont;

typedef struct { int x, y, width, height; } RenRect;
typedef NVGcolor RenColor;

void ren_init(SDL_Window *win);
void ren_resize_window();
void ren_start_frame();
void ren_end_frame();
void ren_update_rects(RenRect *rects, int count);
void ren_set_clip_rect(RenRect rect);
void ren_get_size(int *x, int *y);
void ren_free_window_resources();

int ren_load_font(const char *filename);
void ren_free_font(RenFont *font);
void ren_set_font_size(RenFont *font, float size);
void ren_set_font_tab_size(RenFont *font, int n);
int ren_get_font_tab_size(RenFont *font);

int ren_get_font_width(RenFont *font, const char *text);
int ren_get_font_height(RenFont *font);

void ren_draw_rect(RenRect rect, NVGcolor color);
int ren_draw_text(RenFont *font, const char *text, int x, int y, NVGcolor color);

#endif
