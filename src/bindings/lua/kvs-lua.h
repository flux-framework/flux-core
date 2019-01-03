/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_KVS_LUA_H
#define HAVE_KVS_LUA_H

#include <lua.h>
#include <lauxlib.h>

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

