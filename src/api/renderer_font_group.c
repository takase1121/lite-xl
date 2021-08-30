#include <lua.h>
#include <lauxlib.h>

#include "api.h"
#include "renderer.h"

#define FOREACH(L, n) \
  for (int i = 1, __len = lua_rawlen(L, n); i <= __len; i++) \


void check_metatype(lua_State *L, int n, const char *type) {
  if (lua_getmetatable(L, n)) {
    luaL_getmetatable(L, type);
    int eq = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);

    if (!eq)
      luaL_error(L, "Parameter specified is not a %s object.", type);
  }
}


static int f_new(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_settop(L, 1);
  lua_newtable(L);

  int max_size = 0, max_height = 0;
  for (int i = 1, len = lua_rawlen(L, 1); i <= len; i++) {
    lua_rawgeti(L, 1, i);
    RenFont *font = luaL_checkudata(L, -1, API_TYPE_FONT);
    if (font->size > max_size) max_size = font->size;
    if (font->height > max_height) max_height = font->height;

    lua_pop(L, 1);
    lua_rawseti(L, -1, i);
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

  FOREACH(L, 1) {
    lua_rawgeti(L, 1, i);
    RenFont *font = luaL_checkudata(L, -1, API_TYPE_FONT);
    ren_set_font_tab_size(font, n);
    lua_pop(L, 1);
  }
  
  return 0;
}


static int f_gc(lua_State *L) {
  check_metatype(L, 1, API_TYPE_FONT_GROUP);
  FOREACH(L, 1) {
    lua_rawgeti(L, 1, i);
    RenFont *font = luaL_checkudata(L, -1, API_TYPE_FONT);
    ren_free_font(font);
    lua_pop(L, 1);
  }
  return 0;
}

static int f_get_width(lua_State *L) {
  check_metatype(L, 1, API_TYPE_FONT_GROUP);
  const char *text = luaL_checkstring(L, 2);

  int max_width = 0;
  FOREACH(L, 1) {
    lua_rawgeti(L, 1, i);
    RenFont *font = luaL_checkudata(L, -1, API_TYPE_FONT);
    int width = ren_get_font_width(font, text);
    if (max_width < width) max_width = width;
  }
  lua_pushnumber(L, max_width);
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

  int max_height = 0;
  FOREACH(L, 1) { 
    lua_rawgeti(L, 1, i);
    RenFont *font = luaL_checkudata(L, i, API_TYPE_FONT);
    ren_set_font_size(font, size);
    font->size = size;
    font->height = ren_get_font_height(font);
    if (font->height > max_height) max_height = font->height;
    lua_pop(L, 1);
  }
  lua_pushnumber(L, size);
  lua_setfield(L, -2, "size");

  lua_pushnumber(L, max_height);
  lua_setfield(L, -2, "height");
  return 0;
}


static const luaL_Reg lib[] = {
  { "__gc",               f_gc                 },
  { "__call",             f_new               },
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
