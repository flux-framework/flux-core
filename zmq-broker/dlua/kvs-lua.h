#ifndef HAVE_KVS_LUA_H
#define HAVE_KVS_LUA_H

#include <lua.h>
#include <lauxlib.h>

int luaopen_kvs (lua_State *L);

/* Instantiate userdata on top of stack as a CMB.kvsdir object */
static inline int l_kvsdir_instantiate (lua_State *L)
{
    luaL_getmetatable (L, "CMB.kvsdir");
    lua_setmetatable (L, -2);
    return (1);
}

#endif /* !HAVE_KVS_LUA_H */
