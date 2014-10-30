/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <lua.h>
#include <lauxlib.h>

#include "json-lua.h"
#include "lutil.h"

static int l_json_test (lua_State *L)
{
	int rc;
	json_object *o;
	if (lua_value_to_json (L, -1, &o) < 0) {
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
	lua_newtable (L);
	luaL_setfuncs (L, json_test_functions, 0);
	return (1);
}
