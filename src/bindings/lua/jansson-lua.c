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
#    include "config.h"
#endif
#include <math.h>
#include <errno.h>
#include <lua.h>
#include <lauxlib.h>
#include <jansson.h>

static void *json_nullptr;

static int json_object_to_lua_table (lua_State *L, json_t *o);
static int json_array_to_lua (lua_State *L, json_t *o);

void lua_push_json_null (lua_State *L)
{
    lua_pushlightuserdata (L, json_nullptr);
}

int lua_is_json_null (lua_State *L, int index)
{
    return (lua_touserdata (L, index) == json_null);
}

int json_object_to_lua (lua_State *L, json_t *o)
{
    if (o == NULL) {
        lua_pushnil (L);
        return (1);
    }
    switch (json_typeof (o)) {
    case JSON_OBJECT:
        json_object_to_lua_table (L, o);
        break;
    case JSON_ARRAY:
        json_array_to_lua (L, o);
        break;
    case JSON_STRING:
        lua_pushstring (L, json_string_value (o));
        break;
    case JSON_INTEGER:
        lua_pushinteger (L, json_integer_value (o));
        break;
    case JSON_REAL:
        lua_pushnumber (L, json_real_value (o));
        break;
    case JSON_TRUE:
        lua_pushboolean (L, 1);
        break;
    case JSON_FALSE:
        lua_pushboolean (L, 0);
        break;
    case JSON_NULL:
        /* XXX: crap. */
        break;
    }
    return (1);
}

int json_object_string_to_lua (lua_State *L, const char *json_str)
{
    json_t *o;
    int rc;
    if (!(o = json_loads (json_str, JSON_DECODE_ANY, NULL)))
        return (-1);
    rc = json_object_to_lua (L, o);
    json_decref (o);
    return (rc);
}

static int json_array_to_lua (lua_State *L, json_t *o)
{
    int i;
    int index;
    int n = json_array_size (o);
    lua_newtable (L);
    index = lua_gettop (L);

    for (i = 0; i < n; i++) {
        json_t *entry = json_array_get (o, i);
        if (entry == NULL)
            continue;
        json_object_to_lua (L, entry);
        lua_rawseti (L, index, i + 1);
    }
    return (1);
}

static int json_object_to_lua_table (lua_State *L, json_t *o)
{
    const char *key;
    json_t *value;
    lua_newtable (L);

    json_object_foreach (o, key, value) {
        lua_pushstring (L, key);
        json_object_to_lua (L, value);
        lua_rawset (L, -3);
    }
    return (1);
}

static json_t *lua_table_to_json (lua_State *L, int i);

static int lua_is_integer (lua_State *L, int index)
{
    if (lua_type (L, index) == LUA_TNUMBER) {
        double ip, l = lua_tonumber (L, index);
        if (modf (l, &ip) == 0.0)
            return (1);
    }
    return (0);
}

int lua_value_to_json (lua_State *L, int i, json_t **valp)
{
    int index = (i < 0) ? (lua_gettop (L) + 1) + i : i;
    json_t *o = NULL;

    if (lua_isnoneornil (L, i))
        return (-1);

    switch (lua_type (L, index)) {
    case LUA_TNUMBER:
        if (lua_is_integer (L, index))
            o = json_integer (lua_tointeger (L, index));
        else
            o = json_real (lua_tonumber (L, index));
        break;
    case LUA_TBOOLEAN:
        o = lua_toboolean (L, index) ? json_true () : json_false ();
        break;
    case LUA_TSTRING:
        o = json_string (lua_tostring (L, index));
        break;
    case LUA_TTABLE:
        o = lua_table_to_json (L, index);
        break;
    case LUA_TNIL:
        o = json_object ();
        break;
    case LUA_TLIGHTUSERDATA:
        fprintf (stderr, "Got userdata\n");
        if (lua_touserdata (L, index) == json_null)
            break;
    default:
        luaL_error (L,
                    "Unexpected Lua type %s",
                    lua_typename (L, lua_type (L, index)));
        return (-1);
    }
    *valp = o;
    return (o ? 0 : -1);
}

int lua_value_to_json_string (lua_State *L, int i, char **strp)
{
    json_t *o;
    if (strp == NULL) {
        errno = EINVAL;
        return (-1);
    }
    if (lua_value_to_json (L, i, &o) < 0)
        return (-1);
    if (!(*strp = json_dumps (o, JSON_COMPACT | JSON_ENCODE_ANY)))
        return (-1);
    json_decref (o);
    return (0);
}

static int lua_table_is_array (lua_State *L, int index)
{
    int haskeys = 0;
    lua_pushnil (L);
    while (lua_next (L, index)) {
        haskeys = 1;
        /* If key is not a number abort */
        if (!lua_is_integer (L, -2)) {
            lua_pop (L, 2); /* pop key and value */
            return (0);
        }
        lua_pop (L, 1);
    }
    return (haskeys);
}

static json_t *lua_table_to_json_array (lua_State *L, int index)
{
    int rc;
    json_t *o = json_array ();
    if (o == NULL)
        return (NULL);
    lua_pushnil (L);
    while ((rc = lua_next (L, index))) {
        json_t *val;

        if (lua_value_to_json (L, -1, &val) < 0) {
            json_decref (o);
            return (NULL);
        }
        if (json_array_append_new (o, val) < 0)
            fprintf (stderr, "json_array_append_new failed!\n");
        lua_pop (L, 1);
    }
    return (o);
}

static json_t *lua_table_to_json (lua_State *L, int index)
{
    json_t *o;

    if (!lua_istable (L, index))
        fprintf (stderr,
                 "Object at index=%d is not table, is %s\n",
                 index,
                 lua_typename (L, lua_type (L, index)));

    if (lua_table_is_array (L, index))
        return lua_table_to_json_array (L, index);

    if (!(o = json_object ()))
        return (NULL);
    lua_pushnil (L);
    while (lua_next (L, index)) {
        json_t *val;
        /* -2: key, -1: value */
        const char *key = lua_tostring (L, -2);
        if (lua_value_to_json (L, -1, &val) < 0) {
            json_decref (o);
            return (NULL);
        }
        json_object_set_new (o, key, val);
        /* Remove value, save 'key' for next iteration: */
        lua_pop (L, 1);
    }
    return (o);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
