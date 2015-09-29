#ifndef HAVE_KVS_LUA_H
#define HAVE_KVS_LUA_H

#include <lua.h>
#include <lauxlib.h>

int luaopen_kvs (lua_State *L);

/*
 *  Push kvsdir object onto Lua stack, no external reference taken.
 */
int lua_push_kvsdir (lua_State *L, kvsdir_t *dir);

/*
 *  Push kvsdir object onto Lua stack and increase reference count.
 *   This is used for pushing kvsdir_t objects whicha are created and
 *   destroyed outside of Lua into a Lua stack.
 */
int lua_push_kvsdir_external (lua_State *L, kvsdir_t *dir);

#endif /* !HAVE_KVS_LUA_H */

