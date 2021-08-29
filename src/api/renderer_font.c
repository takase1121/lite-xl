#include <lua.h>
#include <lauxlib.h>

#include "api.h"
#include "renderer.h"

#define GET_FONT(L, n) (lua_rawgeti(L, n, 1), luaL_checknumber(L, -1))

static int f_load(lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  int size = luaL_checknumber(L, 2);
  int font = ren_load_font(filename);
  int line_height = ren_get_font_height(font);
  
  lua_newtable(L);

  lua_pushnumber(L, font);
  lua_rawseti(L, -2, 1);

  lua_pushnumber(L, size);
  lua_setfield(L, -2, "size");

  lua_pushnumber(L, line_height);
  lua_setfield(L, -2, "height");

  luaL_setmetatable(L, API_TYPE_FONT);
  return 1;
}


static int f_copy(lua_State *L) {
  check_metatype(L, 1, API_TYPE_FONT);

  // a simple copy is sufficient
  lua_newtable(L);
  lua_pushnil(L);
  while (lua_next(L, 1) != 0) {
    lua_pushvalue(L, -2);
    lua_insert(L, -2);
    lua_settable(L, -4);
  }

  luaL_setmetatable(L, API_TYPE_FONT);
  return 1;
}


static int f_set_tab_size(lua_State *L) {
  check_metatype(L, 1, API_TYPE_FONT);
  int n = luaL_checknumber(L, 2);

  int font = GET_FONT(L, 1);
  ren_set_font_tab_size(font, n);
  return 0;
}


static int f_gc(lua_State *L) {
  check_metatype(L, 1, API_TYPE_FONT);
  int font = GET_FONT(L, 1);
  ren_free_font(font);
  return 0;
}

static int f_get_width(lua_State *L) {
  check_metatype(L, 1, API_TYPE_FONT);
  const char *text = luaL_checkstring(L, 2);

  int font = GET_FONT(L, 1);
  int w = ren_get_font_width(font, text);
  lua_pushnumber(L, w);
  return 1;
}


static int f_subpixel_scale(lua_State *L) {
  // fprintf(stdout, "warning: no subpixel scale available.\n");
  lua_pushnumber(L, 1);
  return 1;
}

static int f_get_width_subpixel(lua_State *L) {
  // fprintf(stdout, "warning: no subpixel scale available.\n");
  return f_get_width(L);
}


static int f_get_height(lua_State *L) {
  check_metatype(L, 1, API_TYPE_FONT);
  lua_getfield(L, 1, "height");
  return 1;
}


static int f_get_size(lua_State *L) {
  check_metatype(L, 1, API_TYPE_FONT);
  lua_getfield(L, 1, "size");
  return 1;
}


static int f_set_size(lua_State *L) {
  check_metatype(L, 1, API_TYPE_FONT);
  float size = luaL_checknumber(L, 2);

  int font = GET_FONT(L, 1);
  ren_set_font_size(font, size);

  lua_pushnumber(L, size);
  lua_setfield(L, 1, "size");

  lua_pushnumber(L, ren_get_font_height(font));
  lua_setfield(L, 1, "height");

  return 0;
}


static const luaL_Reg lib[] = {
  { "__gc",               f_gc                 },
  { "load",               f_load               },
  { "copy",               f_copy               },
  { "set_tab_size",       f_set_tab_size      },
  { "get_width",          f_get_width          },
  { "get_width_subpixel", f_get_width_subpixel },
  { "get_height",         f_get_height         },
  { "subpixel_scale",     f_subpixel_scale     },
  { "get_size",           f_get_size           },
  { "set_size",           f_set_size           },
  { NULL, NULL }
};

int luaopen_renderer_font(lua_State *L) {
  luaL_newmetatable(L, API_TYPE_FONT);
  luaL_setfuncs(L, lib, 0);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  return 1;
}
