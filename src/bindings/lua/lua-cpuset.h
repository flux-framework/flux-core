/*
 *  Lua cpu_set_t functions that may be used outside of the lua-cpuset
 *   module.
 */
#include <lua.h>

#ifndef _HAVE_LUA_CPUSET_H
#define _HAVE_LUA_CPUSET_H

int lua_get_affinity (lua_State *L);

#endif /* !_HAVE_LUA_CPUSET_H */
