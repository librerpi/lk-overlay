#pragma once

void lua_prettyprint(lua_State *L, int index);
void register_globals(lua_State *L);
int luaL_loadstring(lua_State *L, const char *s);
void* lua_allocator(void *ud, void *ptr, size_t osize, size_t nsize);
int luaL_loadfile(lua_State *L, const char *filename);
