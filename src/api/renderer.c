#include "api.h"
#include "renderer.h"


static NVGcolor checkcolor(lua_State *L, int idx, int def) {
  if (lua_isnoneornil(L, idx)) {
    return nvgRGBA(def, def, def, 255);
  }
  lua_rawgeti(L, idx, 1);
  lua_rawgeti(L, idx, 2);
  lua_rawgeti(L, idx, 3);
  lua_rawgeti(L, idx, 4);
  int r = luaL_checknumber(L, -4);
  int g = luaL_checknumber(L, -3);
  int b = luaL_checknumber(L, -2);
  int a = luaL_optnumber(L, -1, 255);
  lua_pop(L, 4);
  return nvgRGBA(r, g, b, a);
}


static int f_show_debug(lua_State *L) {
  fprintf(stderr, "rip debug\n");
  return 0;
}


static int f_get_size(lua_State *L) {
  int w, h;
  ren_get_size(&w, &h);
  lua_pushnumber(L, w);
  lua_pushnumber(L, h);
  return 2;
}


static int f_begin_frame(lua_State *L) {
  ren_start_frame();
  return 0;
}


static int f_end_frame(lua_State *L) {
  ren_end_frame();
  return 0;
}


static int f_set_clip_rect(lua_State *L) {
  RenRect rect;
  rect.x = luaL_checknumber(L, 1);
  rect.y = luaL_checknumber(L, 2);
  rect.width = luaL_checknumber(L, 3);
  rect.height = luaL_checknumber(L, 4);
  ren_set_clip_rect(rect);
  return 0;
}


static int f_draw_rect(lua_State *L) {
  RenRect rect;
  rect.x = luaL_checknumber(L, 1);
  rect.y = luaL_checknumber(L, 2);
  rect.width = luaL_checknumber(L, 3);
  rect.height = luaL_checknumber(L, 4);
  NVGcolor color = checkcolor(L, 5, 255);
  ren_draw_rect(rect, color);
  return 0;
}

static int draw_text_subpixel_impl(lua_State *L) {
  check_metatype(L, 1, API_TYPE_FONT);
  const char *text = luaL_checkstring(L, 2);
  int x = luaL_checknumber(L, 3);
  int y = luaL_checknumber(L, 4);
  NVGcolor color = checkcolor(L, 5, 255);

  lua_rawgeti(L, 1, 1);
  int font = luaL_checknumber(L, -1);

  x = ren_draw_text(font, text, x, y, color);
  lua_pushnumber(L, x);
  return 1;
}


static int f_draw_text(lua_State *L) {
  return draw_text_subpixel_impl(L);
}


static int f_draw_text_subpixel(lua_State *L) {
  // fprintf(stderr, "subpixel rendering isn't available\n");
  return draw_text_subpixel_impl(L);
}


static const luaL_Reg lib[] = {
  { "show_debug",         f_show_debug         },
  { "get_size",           f_get_size           },
  { "begin_frame",        f_begin_frame        },
  { "end_frame",          f_end_frame          },
  { "set_clip_rect",      f_set_clip_rect      },
  { "draw_rect",          f_draw_rect          },
  { "draw_text",          f_draw_text          },
  { "draw_text_subpixel", f_draw_text_subpixel },
  { NULL,                 NULL                 }
};


int luaopen_renderer_font(lua_State *L);
int luaopen_renderer_font_group(lua_State *L);

int luaopen_renderer(lua_State *L) {
  luaL_newlib(L, lib);
  luaopen_renderer_font(L);
  lua_setfield(L, -2, "font");
  luaopen_renderer_font_group(L);
  lua_setfield(L, -2, "FontGroup");
  return 1;
}
