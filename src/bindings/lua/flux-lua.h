#ifndef HAVE_FLUX_LUA_H
#define HAVE_FLUX_LUA_H

#include <flux/core.h>

/*
 *  Push a flux_t object onto Lua stack for state [L]. Increases the
 *   refcount for [f] since the flux_t object was created outside of
 *   Lua.
 */
int lua_push_flux_handle_external (lua_State *L, flux_t f);

int luaopen_flux (lua_State *L);
#endif /* !HAVE_FLUX_LUA_H */
