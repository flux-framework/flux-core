#ifndef HAVE_KVS_LUA_H
#define HAVE_KVS_LUA_H

#include <lua.h>
#include <lauxlib.h>

int luaopen_kvs (lua_State *L);
int l_push_kvsdir (lua_State *L, kvsdir_t dir);

#endif /* !HAVE_KVS_LUA_H */

