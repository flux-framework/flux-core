
#ifndef HAVE_FLUX_LUA_H
#define HAVE_FLUX_LUA_H
int lua_push_flux_handle (lua_State *L, flux_t f);
int luaopen_flux (lua_State *L);
#endif /* !HAVE_FLUX_LUA_H */
