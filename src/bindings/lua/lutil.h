/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_LUTIL_H
#define HAVE_LUTIL_H

#include <lua.h>
#if !defined LUA_VERSION_NUM || LUA_VERSION_NUM == 501
#define NEED_LUAL_SETFUNCS 1
/*  Lua 5.0/5.1 compatibility with Lua 5.2
 *    http://lua-users.org/wiki/CompatibilityWithLuaFive
 */
#include <lauxlib.h>
void luaL_setfuncs (lua_State *L, const luaL_Reg *l, int nup);
#endif /* LUA_VERSION_NUM <= 501 */

int lua_pusherror (lua_State *L, char *fmt, ...);
int l_pushresult (lua_State *L, int rc);
int l_format_args (lua_State *L, int index);
#endif /* !HAVE_LUTIL_H */
