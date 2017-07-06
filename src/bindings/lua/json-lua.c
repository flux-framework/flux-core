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
#include <math.h>
#include <errno.h>
#include <string.h>
#include <lua.h>
#include <lauxlib.h>
#include "src/common/libjson-c/json.h"

static void * json_null;

static int json_object_to_lua_table (lua_State *L, json_object *o);
static int json_array_to_lua (lua_State *L, json_object *o);

void lua_push_json_null (lua_State *L)
{
    lua_pushlightuserdata (L, json_null);
}

int lua_is_json_null (lua_State *L, int index)
{
    return (lua_touserdata (L, index) == json_null);
}

int json_object_to_lua (lua_State *L, json_object *o)
{
        if (o == NULL)
            lua_pushnil (L);
        switch (json_object_get_type (o)) {
        case json_type_object:
            json_object_to_lua_table (L, o);
            break;
        case json_type_array:
            json_array_to_lua (L, o);
            break;
        case json_type_string:
            lua_pushstring (L, json_object_get_string (o));
            break;
        case json_type_int:
            lua_pushinteger (L, json_object_get_int64 (o));
            break;
        case json_type_double:
            lua_pushnumber (L, json_object_get_double (o));
            break;
        case json_type_boolean:
            lua_pushboolean (L, json_object_get_boolean (o));
            break;
        case json_type_null:
            /* XXX: crap. */
            break;
        }
        return (1);
}

int json_object_string_to_lua (lua_State *L, const char *json_str)
{
    json_object *o;
    int rc;

    if (!(o = json_tokener_parse (json_str))) {
        errno = ENOMEM;
        return (-1);
    }
    rc = json_object_to_lua (L, o);
    json_object_put (o);
    return rc;
}

static int json_array_to_lua (lua_State *L, json_object *o)
{
    int i;
    int index;
    int n = json_object_array_length (o);
    lua_newtable (L);
    index = lua_gettop (L);


    for (i = 0; i < n; i++) {
        json_object *entry = json_object_array_get_idx (o, i);
        if (entry == NULL)
            continue;
        json_object_to_lua (L, entry);
        lua_rawseti (L, index, i+1);
    }
    return (1);
}

static int json_object_to_lua_table (lua_State *L, json_object *o)
{
    json_object_iter iter;

    lua_newtable (L);

    json_object_object_foreachC(o, iter) {
        lua_pushstring (L, iter.key);
        json_object_to_lua (L, iter.val);
        lua_rawset (L, -3);
    }
    return (1);
}

static json_object * lua_table_to_json (lua_State *L, int i);

static int lua_is_integer (lua_State *L, int index)
{
    if (lua_type (L, index) == LUA_TNUMBER) {
        double ip, l = lua_tonumber (L, index);
        if (modf (l, &ip) == 0.0)
            return (1);
    }
    return (0);
}

int lua_value_to_json (lua_State *L, int i, json_object **valp)
{
    int index = (i < 0) ? (lua_gettop (L) + 1) + i : i;
    json_object *o = NULL;

    if (lua_isnoneornil (L, i))
        return (-1);

    switch (lua_type (L, index)) {
        case LUA_TNUMBER:
            if (lua_is_integer (L, index))
                o = json_object_new_int64 (lua_tointeger (L, index));
            else
                o = json_object_new_double (lua_tonumber (L, index));
            break;
        case LUA_TBOOLEAN:
            o = json_object_new_boolean (lua_toboolean (L, index));
            break;
        case LUA_TSTRING:
            o = json_object_new_string (lua_tostring (L, index));
            break;
        case LUA_TTABLE:
            o = lua_table_to_json (L, index);
            break;
        case LUA_TNIL:
            o = json_object_new_object ();
            break;
        case LUA_TLIGHTUSERDATA:
            fprintf (stderr, "Got userdata\n");
            if (lua_touserdata (L, index) == json_null)
                break;
        default:
            luaL_error (L, "Unexpected Lua type %s",
                lua_typename (L, lua_type (L, index)));
            return (-1);
    }
    *valp = o;
    return (0);
}

int lua_value_to_json_string (lua_State *L, int i, char **json_str)
{
    json_object *o;
    char *s;

    if (lua_value_to_json (L, i, &o) < 0)
        return (-1);
    if (!o) {
        errno = ENOMEM;
        return (-1);
    }
    if (!(s = strdup (json_object_to_json_string (o)))) {
        json_object_put (o);
        errno = ENOMEM;
        return (-1);
    }
    json_object_put (o);
    *json_str = s;
    return 0;
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

static json_object * lua_table_to_json_array (lua_State *L, int index)
{
    int rc;
    json_object *o = json_object_new_array ();
    lua_pushnil (L);
    while ((rc = lua_next (L, index))) {
        int i = lua_tointeger (L, -2);
        json_object *val;

        if (lua_value_to_json (L, -1, &val) < 0) {
            json_object_put (o);
            return (NULL);
        }
        json_object_array_put_idx (o, i-1, val);
        lua_pop (L, 1);
    }
    return (o);
}

static json_object * lua_table_to_json (lua_State *L, int index)
{
    json_object *o;

    if (!lua_istable (L, index))
        fprintf (stderr, "Object at index=%d is not table, is %s\n",
                index, lua_typename (L, lua_type (L, index)));

    if (lua_table_is_array (L, index))
        return lua_table_to_json_array (L, index);

    o = json_object_new_object ();
    lua_pushnil (L);
    while (lua_next (L, index)) {
        json_object *val;
        /* -2: key, -1: value */
        const char *key = lua_tostring (L, -2);
        if (lua_value_to_json (L, -1, &val) < 0) {
            json_object_put (o);
            return (NULL);
        }
        json_object_object_add (o, key, val);
        /* Remove value, save 'key' for next iteration: */
        lua_pop (L, 1);
    }
    return (o);
}


/*
 * vi: ts=4 sw=4 expandtab
 */
