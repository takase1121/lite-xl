#include <lua.h>
#include <lauxlib.h>

#include "api.h"
#include "renderer.h"

#define FOREACH_ITABLE(L, n)          \
  int __len = lua_rawlen(L, n);       \
  for (int i = 1; i <= __len; i++)

#define FOREACH_FONT(L, n)                     \
  FOREACH_ITABLE(L, n) {                       \
    lua_rawgeti(L, n, i);                      \
    lua_rawgeti(L, -1, 1);                     \
    int font_handle = luaL_checknumber(L, -1); \
    lua_pop(L, 1);

#define FOREACH_FONT_END }


static int f_load(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_settop(L, 1);
  lua_newtable(L);

  int max_size = 0, max_height = 0;
  FOREACH_ITABLE(L, 1) {
    lua_rawgeti(L, 1, i);
    check_metatype(L, -1, API_TYPE_FONT);

    lua_getfield(L, -1, "size");
    lua_getfield(L, -2, "height");
    int font_size = luaL_checknumber(L, -1);
    int font_height = luaL_checknumber(L, -2);
    if (max_size < font_size) max_size = font_size;
    if (max_height < font_height) max_height = font_height;
    lua_pop(L, 2);

    lua_rawseti(L, 2, i);
  }

  lua_pushnumber(L, max_size);
  lua_setfield(L, -2, "size");

  lua_pushnumber(L, max_height);
  lua_setfield(L, -2, "height");

  luaL_setmetatable(L, API_TYPE_FONT_GROUP);
  return 1;
}


static int f_set_tab_size(lua_State *L) {
  check_metatype(L, 1, API_TYPE_FONT_GROUP);
  int n = luaL_checknumber(L, 2);
  FOREACH_FONT(L, 1) {
    ren_set_font_tab_size(font_handle, n);
    lua_pop(L, 1);
  } FOREACH_FONT_END
  return 0;
}


static int f_gc(lua_State *L) {
  check_metatype(L, 1, API_TYPE_FONT_GROUP);
  FOREACH_FONT(L, 1) {
    ren_free_font(font_handle);
    lua_pop(L, 1);
  } FOREACH_FONT_END
  return 0;
}

static int f_get_width(lua_State *L) {
  check_metatype(L, 1, API_TYPE_FONT_GROUP);
  const char *text = luaL_checkstring(L, 2);

  int max_width = 0;
  FOREACH_FONT(L, 1) {
    int width = ren_get_font_width(font_handle, text);
    if (max_width < width) max_width = width;
  } FOREACH_FONT_END
  lua_pushnumber(L, max_width);
  return 1;
}


static int f_subpixel_scale(lua_State *L) {
  // fprintf(stdout, "warning: no subpixel scale available.\n");
  lua_pushnumber(L, 1);
  return 1;
}

static int f_get_width_subpixel(lua_State *L) {
  // fprintf(stderr, "warning: no subpixel scale available.\n");
  return f_get_width(L);
}

static int f_get_height(lua_State *L) {
  check_metatype(L, 1, API_TYPE_FONT_GROUP);
  lua_getfield(L, 1, "height");
  return 1;
}


static int f_get_size(lua_State *L) {
  check_metatype(L, 1, API_TYPE_FONT_GROUP);
  lua_getfield(L, 1, "size");
  return 1;
}


static int f_set_size(lua_State *L) {
  check_metatype(L, 1, API_TYPE_FONT_GROUP);
  float size = luaL_checknumber(L, 2);
  FOREACH_FONT(L, 1) {
    ren_set_font_size(font_handle, size);
    lua_pushnumber(L, size);
    lua_setfield(L, -2, "size");
  } FOREACH_FONT_END
  return 0;
}


static const luaL_Reg lib[] = {
  { "__gc",               f_gc                 },
  { "__call",             f_load               },
  { "set_tab_size",       f_set_tab_size       },
  { "get_width",          f_get_width          },
  { "get_width_subpixel", f_get_width_subpixel },
  { "get_height",         f_get_height         },
  { "subpixel_scale",     f_subpixel_scale     },
  { "get_size",           f_get_size           },
  { "set_size",           f_set_size           },
  { NULL, NULL }
};

int luaopen_renderer_font_group(lua_State *L) {
  luaL_newmetatable(L, API_TYPE_FONT_GROUP);
  luaL_setfuncs(L, lib, 0);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  return 1;
}
