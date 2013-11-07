
#include <lua.h>
#include <lauxlib.h>

#include "json-lua.h"

static int l_json_test (lua_State *L)
{
	int rc;
	json_object *o = lua_value_to_json (L, -1);
	if (o == NULL) {
		lua_pushnil (L);
		lua_pushstring (L, "lua_value_to_json failure");
		return (2);
	}

	rc = json_object_to_lua (L, o);
	json_object_put (o);
	return (rc);
}

static const struct luaL_Reg json_test_functions [] = {
	{ "runtest",   l_json_test },
	{ NULL,        NULL        }
};

int luaopen_jsontest (lua_State *L)
{
	luaL_register (L, "jsontest", json_test_functions);
	return (1);
}
