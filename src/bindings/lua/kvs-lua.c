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
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <math.h>

#include <lua.h>
#include <lauxlib.h>

#include <flux/core.h>

#include "jansson-lua.h"
#include "lutil.h"

static int l_kvsdir_commit (lua_State *L);

static flux_kvsdir_t *lua_get_kvsdir (lua_State *L, int index)
{
    flux_kvsdir_t **dirp = luaL_checkudata (L, index, "CMB.kvsdir");
    return (*dirp);
}

int l_kvsdir_instantiate (lua_State *L)
{
    luaL_getmetatable (L, "CMB.kvsdir");
    lua_setmetatable (L, -2);
    return (1);
}

static int l_kvsdir_destroy (lua_State *L)
{
    flux_kvsdir_t *d = lua_get_kvsdir (L, -1);
    if (d)
        flux_kvsdir_destroy (d);
    return (0);
}

int lua_push_kvsdir (lua_State *L, flux_kvsdir_t *dir)
{
    flux_kvsdir_t **new;
    if (dir == NULL)
        return lua_pusherror (L, "No such file or directory");
    new = lua_newuserdata (L, sizeof (*new));
    *new = dir;
    return l_kvsdir_instantiate (L);
}

int lua_push_kvsdir_external (lua_State *L, flux_kvsdir_t *dir)
{
    /*
     *  This kvsdir object has been created external to Lua, so take
     *   an extra reference so we don't destroy at garbage collection.
     */
    flux_kvsdir_incref (dir);
    return lua_push_kvsdir (L, dir);
}

static int l_kvsdir_kvsdir_new (lua_State *L)
{
    const char *key;
    flux_kvsdir_t *new;
    flux_kvsdir_t *d;

    d = lua_get_kvsdir (L, 1);
    key = luaL_checkstring (L, 2);

    if (flux_kvsdir_get_dir (d, &new, "%s", key) < 0)
        return lua_pusherror (L, "flux_kvsdir_get_dir: %s",
                              (char *)flux_strerror (errno));

    return lua_push_kvsdir (L, new);
}

static int l_kvsdir_tostring (lua_State *L)
{
    flux_kvsdir_t *d = lua_get_kvsdir (L, 1);
    lua_pushstring (L, flux_kvsdir_key (d));
    return (1);
}

static int l_kvsdir_newindex (lua_State *L)
{
    int rc;
    flux_kvsdir_t *d = lua_get_kvsdir (L, 1);
    const char *key = lua_tostring (L, 2);

    /*
     *  Process value;
     */
    if (lua_isnil (L, 3))
        rc = flux_kvsdir_put (d, key, NULL);
    else if (lua_type (L, 3) == LUA_TNUMBER) {
        double val = lua_tonumber (L, 3);
        if (floor (val) == val)
            rc = flux_kvsdir_pack (d, key, "I", (int64_t) val);
        else
            rc = flux_kvsdir_pack (d, key, "f", val);
    }
    else if (lua_isboolean (L, 3))
        rc = flux_kvsdir_pack (d, key, "b", (int)lua_toboolean (L, 3));
    else if (lua_isstring (L, 3))
        rc = flux_kvsdir_pack (d, key, "s", lua_tostring (L, 3));
    else if (lua_istable (L, 3)) {
        char *json_str;
        lua_value_to_json_string (L, 3, &json_str);
        rc = flux_kvsdir_put (d, key, json_str);
        free (json_str);
    }
    else {
        return luaL_error (L, "Unsupported type for kvs assignment: %s",
                            lua_typename (L, lua_type (L, 3)));
    }
    if (rc < 0)
        return lua_pusherror (L, "flux_kvsdir_put (key=%s, type=%s): %s",
                           key, lua_typename (L, lua_type (L, 3)),
                           flux_strerror (errno));
    return (0);
}

static flux_kvsitr_t *lua_to_kvsitr (lua_State *L, int index)
{
    flux_kvsitr_t **iptr = luaL_checkudata (L, index, "CMB.kvsitr");
    return (*iptr);
}

/* gc metamethod for iterator */
static int l_kvsitr_destroy (lua_State *L)
{
    flux_kvsitr_t *i = lua_to_kvsitr (L, 1);
    flux_kvsitr_destroy (i);
    return (0);
}

static int l_kvsdir_iterator (lua_State *L)
{
    const char *key;
    flux_kvsitr_t *i;

    /* Get kvsitr from upvalue index on stack:
     */
    i = lua_to_kvsitr (L, lua_upvalueindex (1));
    if (i == NULL)
        return (luaL_error (L, "Invalid kvsdir iterator"));

    if ((key = flux_kvsitr_next (i))) {
        lua_pushstring (L, key);
        return (1);
    }

    /* No more keys, return nil */
    return (0);
}

static int l_kvsdir_next (lua_State *L)
{
    flux_kvsdir_t *d = lua_get_kvsdir (L, 1);
    flux_kvsitr_t **iptr;

    lua_pop (L, 1);

    /* Push a flux_kvsitr_t onto top of stack and set its metatable.
     */
    iptr = lua_newuserdata (L, sizeof (*iptr));
    *iptr = flux_kvsitr_create (d);
    luaL_getmetatable (L, "CMB.kvsitr");
    lua_setmetatable (L, -2);

    /* Return iterator function as C closure, with 'iterator, nil, nil'
     *  [ iterator, state, starting value ].  Starting value is always
     *  nil, and we push nil state here because our state is
     *  encoded in the kvsitr of the closure.
     */
    lua_pushcclosure (L, l_kvsdir_iterator, 1);
    return (1);
}

static int l_kvsdir_commit (lua_State *L)
{
    flux_kvsdir_t *d = lua_get_kvsdir (L, 1);
    if (lua_isnoneornil (L, 2)) {
        if (flux_kvs_commit_anon (flux_kvsdir_handle (d), 0) < 0)
            return lua_pusherror (L, "flux_kvs_commit_anon: %s",
                                  (char *)flux_strerror (errno));
    }
    lua_pushboolean (L, true);
    return (1);
}

static int l_kvsdir_unlink (lua_State *L)
{
    flux_kvsdir_t *d = lua_get_kvsdir (L, 1);
    const char *key = luaL_checkstring (L, 2);
    if (flux_kvsdir_unlink (d, key) < 0)
            return lua_pusherror (L, "unlink: %s",
                                  (char *)flux_strerror (errno));
    lua_pushboolean (L, true);
    return (1);
}


static int l_kvsdir_watch (lua_State *L)
{
    int rc;
    void *h;
    char *key;
    char *json_str = NULL;
    flux_kvsdir_t *dir;

    dir = lua_get_kvsdir (L, 1);
    h = flux_kvsdir_handle (dir);
    key = flux_kvsdir_key_at (dir, lua_tostring (L, 2));

    if (lua_isnoneornil (L, 3)) {
        /* Need to fetch initial value */
        if (((rc = flux_kvs_get (h, key, &json_str)) < 0) && (errno != ENOENT))
            goto err;
    }
    else {
        /*  Otherwise, the alue at top of stack is initial json_object */
        lua_value_to_json_string (L, -1, &json_str);
    }

    rc = flux_kvs_watch_once (h, key, &json_str);
err:
    free (key);
    if (rc < 0)
        return lua_pusherror (L, "flux_kvs_watch: %s",
                              (char *)flux_strerror (errno));

    json_object_string_to_lua (L, json_str);
    free (json_str);
    return (1);
}

static int l_kvsdir_watch_dir (lua_State *L)
{
    flux_t *h;
    flux_kvsdir_t *dir;

    dir = lua_get_kvsdir (L, 1);
    h = flux_kvsdir_handle (dir);

    return l_pushresult (L, flux_kvs_watch_once_dir (h, &dir, "%s",
                         flux_kvsdir_key (dir)));
}

static int l_kvsdir_index (lua_State *L)
{
    int rc;
    flux_t *f;
    flux_kvsdir_t *d;
    const char *key = lua_tostring (L, 2);
    char *fullkey = NULL;
    char *json_str = NULL;

    if (key == NULL)
        return luaL_error (L, "kvsdir: invalid index");
    /*
     *  To support indeces like kvsdir ["a.relative.path"] we have
     *   to pretend that kvsdir objects support this kind of
     *   non-local indexing by using full paths and flux handle :-(
     */
    d = lua_get_kvsdir (L, 1);
    f = flux_kvsdir_handle (d);
    fullkey = flux_kvsdir_key_at (d, key);

    if (flux_kvs_get (f, fullkey, &json_str) == 0)
        rc = json_object_string_to_lua (L, json_str);
    else if (errno == EISDIR)
        rc = l_kvsdir_kvsdir_new (L);
    else {
        /* No key. First check metatable to see if this is a method */
        lua_getmetatable (L, 1);
        lua_getfield (L, -1, key);

        /* If not then return error */
        if (lua_isnil (L, -1)) {
             rc = lua_pusherror (L, "Key not found.");
            goto out;
        }
        rc = 1;
    }
out:
    free (fullkey);
    free (json_str);
    return (rc);
}

#if 0
static const struct luaL_Reg kvsdir_functions [] = {
    { "commit",          l_kvsdir_commit    },
    { "keys",            l_kvsdir_next      },
    { "watch",           l_kvsdir_watch     },
    { "watch_dir",       l_kvsdir_watch_dir },
    { NULL,              NULL               },
};
#endif

static const struct luaL_Reg kvsitr_methods [] = {
    { "__gc",           l_kvsitr_destroy   },
    { NULL,              NULL              }
};

static const struct luaL_Reg kvsdir_methods [] = {
    { "__gc",            l_kvsdir_destroy  },
    { "__index",         l_kvsdir_index    },
    { "__newindex",      l_kvsdir_newindex },
    { "__tostring",      l_kvsdir_tostring },
    { "commit",          l_kvsdir_commit   },
    { "unlink",          l_kvsdir_unlink   },
    { "keys",            l_kvsdir_next     },
    { "watch",           l_kvsdir_watch    },
    { "watch_dir",       l_kvsdir_watch_dir},
    { NULL,              NULL              }
};

int l_kvsdir_register_metatable (lua_State *L)
{
    luaL_newmetatable (L, "CMB.kvsitr");
    luaL_setfuncs (L, kvsitr_methods, 0);
    luaL_newmetatable (L, "CMB.kvsdir");
    luaL_setfuncs (L, kvsdir_methods, 0);
    return (1);
}

int luaopen_kvs (lua_State *L)
{
    l_kvsdir_register_metatable (L);
    //lua_newtable (L);
    //luaL_setfuncs (L, kvsdir_functions, 0);
    return (1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
