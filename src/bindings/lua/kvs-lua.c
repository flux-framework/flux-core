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

#include <assert.h>

#include <flux/core.h>

#include "jansson-lua.h"
#include "lutil.h"

static int l_kvsdir_commit (lua_State *L);

/* aukey shared between wreck, lua, and kz */
const char *lua_default_txn_auxkey = "flux::wreck_lua_kz_txn";
flux_kvs_txn_t *lua_kvs_get_default_txn (flux_t *h)
{
    flux_kvs_txn_t *txn = NULL;

    assert (h);

    if (!(txn = flux_aux_get (h, lua_default_txn_auxkey))) {
        if (!(txn = flux_kvs_txn_create ()))
            goto done;
        flux_aux_set (h, lua_default_txn_auxkey,
                      txn, (flux_free_f)flux_kvs_txn_destroy);
    }
 done:
    return txn;
}

void lua_kvs_clear_default_txn (flux_t *h)
{
    flux_aux_set (h, lua_default_txn_auxkey, NULL, NULL);
}

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
    flux_t *h;
    const char *rootref;
    char *keyat = NULL;
    flux_future_t *f = NULL;
    const flux_kvsdir_t *subdir;
    int rc = -1;

    d = lua_get_kvsdir (L, 1);
    key = luaL_checkstring (L, 2);
    h = flux_kvsdir_handle (d);
    rootref = flux_kvsdir_rootref (d);

    if (!(keyat = flux_kvsdir_key_at (d, key))) {
        rc = lua_pusherror (L, "flux_kvsdir_key_at: %s",
                            (char *)flux_strerror (errno));
        goto err;
    }
    if (!(f = flux_kvs_lookupat (h, FLUX_KVS_READDIR, keyat, rootref))) {
        rc = lua_pusherror (L, "flux_kvs_lookupat: %s",
                            (char *)flux_strerror (errno));
        goto err;
    }
    if (flux_kvs_lookup_get_dir (f, &subdir) < 0) {
        rc = lua_pusherror (L, "flux_kvs_lookup_get_dir: %s",
                            (char *)flux_strerror (errno));
        goto err;
    }
    if (!(new = flux_kvsdir_copy (subdir))) {
        rc = lua_pusherror (L, "flux_kvsdir_copy: %s",
                            (char *)flux_strerror (errno));
        goto err;
    }

    rc = lua_push_kvsdir (L, new);
err:
    free (keyat);
    flux_future_destroy (f);
    return rc;
}

static int l_kvsdir_tostring (lua_State *L)
{
    flux_kvsdir_t *d = lua_get_kvsdir (L, 1);
    lua_pushstring (L, flux_kvsdir_key (d));
    return (1);
}

static int l_kvsdir_newindex (lua_State *L)
{
    int rc = 0;
    int ret = -1;
    flux_kvsdir_t *d = lua_get_kvsdir (L, 1);
    flux_t *h = flux_kvsdir_handle (d);
    const char *key = lua_tostring (L, 2);
    flux_kvs_txn_t *txn;
    char *keyat = NULL;

    if (!(txn = lua_kvs_get_default_txn (h))) {
        rc = lua_pusherror (L, "cannot get default transaction");
        goto done;
    }

    if (!(keyat = flux_kvsdir_key_at (d, key))) {
        rc = lua_pusherror (L, "flux_kvsdir_key_at: %s",
                            (char *)flux_strerror (errno));
        goto done;
    }

    /*
     *  Process value;
     */
    if (lua_isnil (L, 3))
        ret = flux_kvs_txn_unlink (txn, 0, keyat);
    else if (lua_type (L, 3) == LUA_TNUMBER) {
        double val = lua_tonumber (L, 3);
        if (floor (val) == val)
            ret = flux_kvs_txn_pack (txn, 0, keyat, "I", (int64_t) val);
        else
            ret = flux_kvs_txn_pack (txn, 0, keyat, "f", val);
    }
    else if (lua_isboolean (L, 3))
        ret = flux_kvs_txn_pack (txn, 0, keyat, "b", (int)lua_toboolean (L, 3));
    else if (lua_isstring (L, 3))
        ret = flux_kvs_txn_pack (txn, 0, keyat, "s", lua_tostring (L, 3));
    else if (lua_istable (L, 3)) {
        char *json_str;
        lua_value_to_json_string (L, 3, &json_str);
        ret = flux_kvs_txn_put (txn, 0, keyat, json_str);
        free (json_str);
    }
    else {
        rc = luaL_error (L, "Unsupported type for kvs assignment: %s",
                         lua_typename (L, lua_type (L, 3)));
        goto done;
    }

    if (ret < 0)
        rc = lua_pusherror (L, "flux_kvs_txn_put/pack (key=%s, type=%s): %s",
                            key, lua_typename (L, lua_type (L, 3)),
                            flux_strerror (errno));
done:
    free (keyat);
    return (rc);
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
        flux_t *h;
        flux_kvs_txn_t *txn;
        flux_future_t *future;

        h = flux_kvsdir_handle (d);

        if (!(txn = lua_kvs_get_default_txn (h)))
            return lua_pusherror (L, "cannot get default transaction");

        if (!(future = flux_kvs_commit (h, 0, txn)))
            return lua_pusherror (L, "flux_kvs_commit: %s",
                                  (char *)flux_strerror (errno));

        if (flux_future_get (future, NULL) < 0) {
            int saved_errno = errno;
            flux_future_destroy (future);
            lua_kvs_clear_default_txn (h);
            errno = saved_errno;
            return lua_pusherror (L, (char *)flux_strerror (errno));
        }

        flux_future_destroy (future);
        lua_kvs_clear_default_txn (h);
    }
    lua_pushboolean (L, true);
    return (1);
}

static int l_kvsdir_unlink (lua_State *L)
{
    flux_kvsdir_t *d = lua_get_kvsdir (L, 1);
    const char *key = luaL_checkstring (L, 2);
    flux_t *h = flux_kvsdir_handle (d);
    flux_kvs_txn_t *txn;
    char *keyat = NULL;
    int rc = -1;

    if (!(txn = lua_kvs_get_default_txn (h))) {
        rc = lua_pusherror (L, "cannot get default transaction");
        goto done;
    }

    if (!(keyat = flux_kvsdir_key_at (d, key))) {
        rc = lua_pusherror (L, "flux_kvsdir_key_at: %s",
                            (char *)flux_strerror (errno));
        goto done;
    }

    if (flux_kvs_txn_unlink (txn, 0, keyat) < 0) {
        rc = lua_pusherror (L, "flux_kvs_txn_unlink: %s",
                            (char *)flux_strerror (errno));
        goto done;
    }

    lua_pushboolean (L, true);
    rc = 1;
done:
    free (keyat);
    return (rc);
}


static int l_kvsdir_watch (lua_State *L)
{
    int rc;
    void *h;
    char *key;
    char *json_str = NULL;
    const char *json_str_ptr;
    flux_kvsdir_t *dir;
    flux_future_t *f = NULL;

    dir = lua_get_kvsdir (L, 1);
    h = flux_kvsdir_handle (dir);
    key = flux_kvsdir_key_at (dir, lua_tostring (L, 2));

    if (lua_isnoneornil (L, 3)) {
        /* Need to fetch initial value */
        if (!(f = flux_kvs_lookup (h, 0, key))) {
            rc = -1;
            goto err;
        }
        if ((rc = flux_kvs_lookup_get (f, &json_str_ptr)) < 0
            && (errno != ENOENT))
            goto err;
    }
    else {
        /*  Otherwise, the value at top of stack is initial json_object */
        lua_value_to_json_string (L, -1, &json_str);
        json_str_ptr = json_str;
    }

    rc = flux_kvs_watch_once (h, key, &json_str);
err:
    free (key);
    if (rc < 0)
        return lua_pusherror (L, "flux_kvs_watch: %s",
                              (char *)flux_strerror (errno));

    json_object_string_to_lua (L, json_str);
    free (json_str);
    flux_future_destroy (f);
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
    const char *json_str;
    flux_future_t *future = NULL;

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

    if ((future = flux_kvs_lookup (f, 0, fullkey))
        && flux_kvs_lookup_get (future, &json_str) == 0)
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
    flux_future_destroy (future);
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
