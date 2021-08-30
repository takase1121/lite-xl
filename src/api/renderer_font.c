#include <lua.h>
#include <lauxlib.h>

#include "api.h"
#include "renderer.h"

static int f_load(lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  float size = luaL_checknumber(L, 2);

  int font_handle = ren_load_font(filename);
  RenFont *font = lua_newuserdata(L, sizeof(RenFont));
  font->handle = font_handle;
  font->size = size;
  font->height = ren_get_font_height(font);

  luaL_setmetatable(L, API_TYPE_FONT);
  return 1;
}


static int f_copy(lua_State *L) {
  RenFont *font = luaL_checkudata(L, 1, API_TYPE_FONT);
  float size = luaL_optnumber(L, 2, font->size);

  RenFont *new_font = lua_newuserdata(L, sizeof(RenFont));
  new_font->handle = font->handle;
  new_font->size = size;
  new_font->height = ren_get_font_height(new_font);

  luaL_setmetatable(L, API_TYPE_FONT);
  return 1;
}


static int f_set_tab_size(lua_State *L) {
  RenFont *font = luaL_checkudata(L, 1, API_TYPE_FONT);
  int n = luaL_checknumber(L, 2);

  ren_set_font_tab_size(font, n);
  return 0;
}


static int f_gc(lua_State *L) {
  RenFont *font = luaL_checkudata(L, 1, API_TYPE_FONT);
  ren_free_font(font);
  return 0;
}

static int f_get_width(lua_State *L) {
  RenFont *font = luaL_checkudata(L, 1, API_TYPE_FONT);
  const char *text = luaL_checkstring(L, 2);

  lua_pushnumber(L, ren_get_font_width(font, text));
  return 1;
}


static int f_subpixel_scale(lua_State *L) {
  lua_pushnumber(L, 1);
  return 1;
}

static int f_get_width_subpixel(lua_State *L) {
  return f_get_width(L);
}


static int f_get_height(lua_State *L) {
  RenFont *font = luaL_checkudata(L, 1, API_TYPE_FONT);
  lua_pushnumber(L, font->height);
  return 1;
}


static int f_get_size(lua_State *L) {
  RenFont *font = luaL_checkudata(L, 1, API_TYPE_FONT);
  lua_pushnumber(L, font->size);
  return 1;
}


static int f_set_size(lua_State *L) {
  RenFont *font = luaL_checkudata(L, 1, API_TYPE_FONT);
  float size = luaL_checknumber(L, 2);

  ren_set_font_size(font, size);
  font->size = size;
  font->height = ren_get_font_height(font);

  return 0;
}


static int f_add_fallback(lua_State *L) {
  RenFont *basefont = luaL_checkudata(L, 1, API_TYPE_FONT);
  RenFont *font = luaL_checkudata(L, 2, API_TYPE_FONT);
  ren_add_fallback_font(basefont, font);
  lua_pushvalue(L, 1);
  return 1;
}


static int f_reset_fallbacks(lua_State *L) {
  RenFont *font = luaL_checkudata(L, 1, API_TYPE_FONT);
  ren_reset_fallback_fonts(font);
  return 0;
}


static const luaL_Reg lib[] = {
  { "__gc",               f_gc                 },
  { "load",               f_load               },
  { "copy",               f_copy               },
  { "set_tab_size",        f_set_tab_size      },
  { "get_width",          f_get_width          },
  { "get_width_subpixel", f_get_width_subpixel },
  { "get_height",         f_get_height         },
  { "subpixel_scale",     f_subpixel_scale     },
  { "get_size",           f_get_size           },
  { "set_size",           f_set_size           },
  { "add_fallback",       f_add_fallback       },
  { "reset_fallbacks",    f_reset_fallbacks    },
  { NULL, NULL }
};

int luaopen_renderer_font(lua_State *L) {
  luaL_newmetatable(L, API_TYPE_FONT);
  luaL_setfuncs(L, lib, 0);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  return 1;
}
