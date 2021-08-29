#include "api.h"


int luaopen_system(lua_State *L);
int luaopen_renderer(lua_State *L);
int luaopen_regex(lua_State *L);
int luaopen_process(lua_State *L);


static const luaL_Reg libs[] = {
  { "system",    luaopen_system     },
  { "renderer",  luaopen_renderer   },
  { "regex",     luaopen_regex   },
  { "process",   luaopen_process    },
  { NULL, NULL }
};


void check_metatype(lua_State *L, int n, const char *type) {
  if (lua_getmetatable(L, n)) {
    luaL_getmetatable(L, type);
    int eq = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);

    if (!eq)
      luaL_error(L, "Parameter specified is not a %s object.", type);
  }
}


void api_load_libs(lua_State *L) {
  for (int i = 0; libs[i].name; i++) {
    luaL_requiref(L, libs[i].name, libs[i].func, 1);
  }
}
