#ifndef API_H
#define API_H

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define API_TYPE_FONT "Font"
#define API_TYPE_FONT_GROUP "FontGroup"
#define API_TYPE_REPLACE "Replace"
#define API_TYPE_PROCESS "Process"

void check_metatype(lua_State *L, int n, const char *type);
void api_load_libs(lua_State *L);

#endif
