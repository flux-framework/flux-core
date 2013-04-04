
#ifndef HAVE_PEPE_LUA_H
#define HAVE_PEPE_LUA_H
typedef struct pepe_lua * pepe_lua_t;

pepe_lua_t pepe_lua_state_create (int nprocs, int rank);
void       pepe_lua_state_destroy (pepe_lua_t l);
int        pepe_lua_script_execute (pepe_lua_t l, const char *name);
#endif

