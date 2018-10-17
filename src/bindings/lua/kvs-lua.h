#ifndef HAVE_KVS_LUA_H
#define HAVE_KVS_LUA_H

#include <lua.h>
#include <lauxlib.h>

/* wrappers to store a default txn for all kvs write operations */
flux_kvs_txn_t *lua_kvs_get_default_txn (flux_t *h);
void lua_kvs_clear_default_txn (flux_t *h);

int luaopen_kvs (lua_State *L);

/*
 *  Push flux_kvsdir_t object onto Lua stack, no external reference taken.
 */
int lua_push_kvsdir (lua_State *L, flux_kvsdir_t *dir);

/*
 *  Push flux_kvsdir_t object onto Lua stack and increase reference count.
 *   This is used for pushing flux_kvsdir_t objects whicha are created and
 *   destroyed outside of Lua into a Lua stack.
 */
int lua_push_kvsdir_external (lua_State *L, flux_kvsdir_t *dir);

#endif /* !HAVE_KVS_LUA_H */

