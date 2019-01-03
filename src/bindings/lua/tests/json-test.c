/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <lua.h>
#include <lauxlib.h>

#include "jansson-lua.h"
#include "lutil.h"

static int l_json_test (lua_State *L)
{
	int rc;
	char *json_str = NULL;
	if (lua_value_to_json_string (L, -1, &json_str) < 0) {
		lua_pushnil (L);
		lua_pushstring (L, "lua_value_to_json failure");
		return (2);
	}
	rc = json_object_string_to_lua (L, json_str);
	free (json_str);
	return (rc);
}

static const struct luaL_Reg json_test_functions [] = {
	{ "runtest",   l_json_test },
	{ NULL,        NULL        }
};

int luaopen_jsontest (lua_State *L)
{
	lua_newtable (L);
	luaL_setfuncs (L, json_test_functions, 0);
	return (1);
}
