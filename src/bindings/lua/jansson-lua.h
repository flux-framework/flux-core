/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_JANSSON_LUA
#define HAVE_JANSSON_LUA
#include <lua.h>
#include <jansson.h>

void lua_push_json_null (lua_State *L);
int lua_is_json_null (lua_State *L, int index);

int json_object_to_lua (lua_State *L, json_t *o);
int json_object_string_to_lua (lua_State *L, const char *json_str);

int lua_value_to_json (lua_State *L, int index, json_t **v);
int lua_value_to_json_string (lua_State *L, int index, char **json_str);

#endif /* HAVE_JANSSON_LUA */
