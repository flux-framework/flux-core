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

#include <lua.h>
#include <lauxlib.h>

#include "flux/core.h"
#include "src/common/libcompat/compat.h"
#include "src/modules/kvs/kvs_deprecated.h"

#include "json-lua.h"
#include "lutil.h"

static int l_kvsdir_commit (lua_State *L);

static kvsdir_t *lua_get_kvsdir (lua_State *L, int index)
{
    kvsdir_t **dirp = luaL_checkudata (L, index, "CMB.kvsdir");
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
    kvsdir_t *d = lua_get_kvsdir (L, -1);
    if (d)
        kvsdir_destroy (d);
    return (0);
}

int lua_push_kvsdir (lua_State *L, kvsdir_t *dir)
{
    kvsdir_t **new;
    if (dir == NULL)
        return lua_pusherror (L, "No such file or directory");
    new = lua_newuserdata (L, sizeof (*new));
    *new = dir;
    return l_kvsdir_instantiate (L);
}

int lua_push_kvsdir_external (lua_State *L, kvsdir_t *dir)
{
    /*
     *  This kvsdir object has been created external to Lua, so take
     *   an extra reference so we don't destroy at garbage collection.
     */
    kvsdir_incref (dir);
    return lua_push_kvsdir (L, dir);
}

static int l_kvsdir_kvsdir_new (lua_State *L)
{
    const char *key;
    kvsdir_t *new;
    kvsdir_t *d;

    d = lua_get_kvsdir (L, 1);
    key = luaL_checkstring (L, 2);

    if (kvsdir_get_dir (d, &new, "%s", key) < 0)
        return lua_pusherror (L, "kvsdir_get_dir: %s",
                              (char *)flux_strerror (errno));

    return lua_push_kvsdir (L, new);
}

static int l_kvsdir_tostring (lua_State *L)
{
    kvsdir_t *d = lua_get_kvsdir (L, 1);
    lua_pushstring (L, kvsdir_key (d));
    return (1);
}

static int l_kvsdir_newindex (lua_State *L)
{
    int rc;
    kvsdir_t *d = lua_get_kvsdir (L, 1);
    const char *key = lua_tostring (L, 2);

    /*
     *  Process value;
     */
    if (lua_isnil (L, 3))
        rc = kvsdir_put_obj (d, key, NULL);
    else if (lua_type (L, 3) == LUA_TNUMBER) {
        double val = lua_tonumber (L, 3);
        if (floor (val) == val)
            rc = kvsdir_put_int64 (d, key, (int64_t) val);
        else
            rc = kvsdir_put_double (d, key, val);
    }
    else if (lua_isboolean (L, 3))
        rc = kvsdir_put_boolean (d, key, lua_toboolean (L, 3));
    else if (lua_isstring (L, 3))
        rc = kvsdir_put_string (d, key, lua_tostring (L, 3));
    else if (lua_istable (L, 3)) {
        json_object *o;
        lua_value_to_json (L, 3, &o);
        rc = kvsdir_put_obj (d, key, o);
        json_object_put (o);
    }
    else {
        return luaL_error (L, "Unsupported type for kvs assignment: %s",
                            lua_typename (L, lua_type (L, 3)));
    }
    if (rc < 0)
        return lua_pusherror (L, "kvsdir_put (key=%s, type=%s): %s",
                           key, lua_typename (L, lua_type (L, 3)),
                           flux_strerror (errno));
    return (0);
}

static kvsitr_t *lua_to_kvsitr (lua_State *L, int index)
{
    kvsitr_t **iptr = luaL_checkudata (L, index, "CMB.kvsitr");
    return (*iptr);
}

/* gc metamethod for iterator */
static int l_kvsitr_destroy (lua_State *L)
{
    kvsitr_t *i = lua_to_kvsitr (L, 1);
    kvsitr_destroy (i);
    return (0);
}

static int l_kvsdir_iterator (lua_State *L)
{
    const char *key;
    kvsitr_t *i;

    /* Get kvsitr from upvalue index on stack:
     */
    i = lua_to_kvsitr (L, lua_upvalueindex (1));
    if (i == NULL)
        return (luaL_error (L, "Invalid kvsdir iterator"));

    if ((key = kvsitr_next (i))) {
        lua_pushstring (L, key);
        return (1);
    }

    /* No more keys, return nil */
    return (0);
}

static int l_kvsdir_next (lua_State *L)
{
    kvsdir_t *d = lua_get_kvsdir (L, 1);
    kvsitr_t **iptr;

    lua_pop (L, 1);

    /* Push a kvsitr_t onto top of stack and set its metatable.
     */
    iptr = lua_newuserdata (L, sizeof (*iptr));
    *iptr = kvsitr_create (d);
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
    kvsdir_t *d = lua_get_kvsdir (L, 1);
    if (lua_isnoneornil (L, 2)) {
        if (kvs_commit (kvsdir_handle (d)) < 0)
            return lua_pusherror (L, "kvs_commit: %s",
                                  (char *)flux_strerror (errno));
    }
    lua_pushboolean (L, true);
    return (1);
}

static int l_kvsdir_unlink (lua_State *L)
{
    kvsdir_t *d = lua_get_kvsdir (L, 1);
    const char *key = luaL_checkstring (L, 2);
    if (kvsdir_unlink (d, key) < 0)
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
    json_object *o;
    kvsdir_t *dir;

    dir = lua_get_kvsdir (L, 1);
    h = kvsdir_handle (dir);
    key = kvsdir_key_at (dir, lua_tostring (L, 2));

    if (lua_isnoneornil (L, 3)) {
        /* Need to fetch initial value */
        if (((rc = kvs_get_obj (h, key, &o)) < 0) && (errno != ENOENT))
            goto err;
    }
    else {
        /*  Otherwise, the alue at top of stack is initial json_object */
        lua_value_to_json (L, -1, &o);
    }

    rc = kvs_watch_once_obj (h, key, &o);
err:
    free (key);
    if (rc < 0)
        return lua_pusherror (L, "kvs_watch: %s",
                              (char *)flux_strerror (errno));

    json_object_to_lua (L, o);
    json_object_put (o);
    return (1);
}

static int l_kvsdir_watch_dir (lua_State *L)
{
    flux_t h;
    kvsdir_t *dir;

    dir = lua_get_kvsdir (L, 1);
    h = kvsdir_handle (dir);

    return l_pushresult (L, kvs_watch_once_dir (h, &dir, "%s", kvsdir_key (dir)));
}

static int l_kvsdir_index (lua_State *L)
{
    int rc;
    flux_t f;
    kvsdir_t *d;
    const char *key = lua_tostring (L, 2);
    char *fullkey = NULL;
    json_object *o = NULL;

    if (key == NULL)
        return luaL_error (L, "kvsdir: invalid index");
    /*
     *  To support indeces like kvsdir ["a.relative.path"] we have
     *   to pretend that kvsdir objects support this kind of
     *   non-local indexing by using full paths and flux handle :-(
     */
    d = lua_get_kvsdir (L, 1);
    f = kvsdir_handle (d);
    fullkey = kvsdir_key_at (d, key);

    if (kvs_get_obj (f, fullkey, &o) == 0)
        rc = json_object_to_lua (L, o);
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
    if (o)
        json_object_put (o);
    free (fullkey);
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
