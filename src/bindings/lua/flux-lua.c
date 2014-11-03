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

#include "src/common/libutil/jsonutil.h"
#include "src/common/libmrpc/mrpc.h"
#include "src/common/libzio/zio.h"
#include "src/common/libzio/kz.h"

#include "json-lua.h"
#include "kvs-lua.h"
#include "zmsg-lua.h"
#include "lutil.h"

/*
 *  Create a table in the registry referencing the flux userdata
 *   that is currently at position [index] on the stack.
 */
static int lua_flux_obj_ref_create (lua_State *L, int index)
{
    int top = lua_gettop (L);
    int i = index < 0 ? top + index + 1 : index;

    assert (lua_isuserdata (L, i));

    lua_newtable (L);
    /*
     *  We don't want this reference for the flux userdata to
     *   count for GC, so we store the flux userdata object in
     *   a subtable with weak values set
     */
    lua_newtable (L);
    lua_pushliteral (L, "__mode");
    lua_pushliteral (L, "v");
    lua_rawset (L, -3);
    lua_setmetatable (L, -2);

    lua_pushvalue (L, i);
    lua_rawseti (L, -2, 1);  /* t[1] = userdata */

    /* Return new flux obj reference table on top of stack */
    return (1);
}


/*
 *  The flux 'reftable' is a table of references to flux C userdata
 *   and other object types for the flux Lua support. It is used to
 *   keep references to userdata objects that are currently extant,
 *   for storing ancillary data for these references, and as a lookup
 *   table for the 'flux' userdata object corresponding to a given
 *   flux_t C object.
 *
 *  The reftable is stored by the address of the flux_t [f] pointer
 *   (using lightuserdata), and contains at least the following tables:
 *     flux = { userdata },
 *     msghandler = { table of msghandler tables },
 *     ...
 *
 *  The flux userdata itself is stored in a separate table so that
 *   it can be referenced "weak", that is we use this table to translate
 *   between flux_t C object and flux userdata Lua object, not to
 *   store an extra reference.
 *
 */
static int l_get_flux_reftable (lua_State *L, flux_t f)
{
    /*
     *  Use flux handle as lightuserdata index into registry.
     *   This allows multiple flux handles per lua_State.
     */
    lua_pushlightuserdata (L, (void *)f);
    lua_gettable (L, LUA_REGISTRYINDEX);

    if (lua_isnil (L, -1)) {
        lua_pop (L, 1);
        /*   New table indexed by flux handle address */
        lua_pushlightuserdata (L, (void *)f);
        lua_newtable (L);
        lua_settable (L, LUA_REGISTRYINDEX);

        /*  Create internal flux table entries */

        /*  Get table again */
        lua_pushlightuserdata (L, (void *)f);
        lua_gettable (L, LUA_REGISTRYINDEX);

        lua_newtable (L);
        lua_setfield (L, -2, "msghandler");
        lua_newtable (L);
        lua_setfield (L, -2, "kvswatcher");
        lua_newtable (L);
        lua_setfield (L, -2, "iowatcher");
    }

    return (1);
}

/*
 *  When we push a flux_t handle [f], we first check to see if
 *   there is an existing flux reftable, and if so we just return
 *   a reference to the existing object. Otherwise, create the reftable
 *   and return the new object.
 */
int lua_push_flux_handle (lua_State *L, flux_t f)
{
    flux_t *fp;
    int top = lua_gettop (L);

    /*
     *  First see if this flux_t object already has a lua component:
     */
    l_get_flux_reftable (L, f);  /* [ reftable, ... ] */
    lua_pushliteral (L, "flux"); /* [ flux, reftable, ... ] */
    lua_rawget (L, -2);          /* [ value, reftable, ... ] */
    if (lua_istable (L, -1)) {
        lua_rawgeti (L, -1, 1);  /* [ userdata, table, reftable, ... ] */

        assert (lua_isuserdata (L, -1));

        /* Restore stack with userdata on top */
        lua_replace (L, top+1);  /* [ table, userdata, ... ] */
        lua_settop (L, top+1);   /* [ userdata, .... ] */

        return (1);
    }

    /*
     *  Otherwise create a new object:
     */
    fp = lua_newuserdata (L, sizeof (*fp));
    *fp = f;
    luaL_getmetatable (L, "FLUX.handle");
    lua_setmetatable (L, -2);

    /*
     *  Set flux weak key reference table in flux reftable such that
     *    reftable = {
     *        flux = { [1] = <userdata> }. -- with mettable { __mode = v }
     *        ...
     *   }
     */
    l_get_flux_reftable (L, f);     /* [ table, udata, ... ] */
    lua_pushliteral (L, "flux");    /* [ 'flux', table, udata, ... ]     */
    lua_flux_obj_ref_create (L, -3);/* [ objref, 'flux', t, udata, ... ] */
    lua_rawset (L, -3);             /* reftable.flux = ...               */
                                    /*  [ t, udata, ... ]                */
    lua_pop (L, 1);                 /* pop reftable leaving userdata     */

    return (1);
}

static void l_flux_reftable_unref (lua_State *L, flux_t f)
{
    l_get_flux_reftable (L, f);
    if (lua_istable (L, -1)) {
        lua_pushliteral (L, "flux");
        lua_pushnil (L);
        lua_rawset (L, -3);
    }
}

static flux_t lua_get_flux (lua_State *L, int index)
{
    flux_t *fluxp = luaL_checkudata (L, index, "FLUX.handle");
    return (*fluxp);
}

static int l_flux_destroy (lua_State *L)
{
    flux_t f = lua_get_flux (L, 1);
    l_flux_reftable_unref (L, f);
    flux_handle_destroy (&f);
    return (0);
}

static int l_flux_new (lua_State *L)
{
    flux_t f = flux_api_open ();
    if (f == NULL)
        return lua_pusherror (L, strerror (errno));
    return (lua_push_flux_handle (L, f));
}

static int l_flux_kvsdir_new (lua_State *L)
{
    const char *path = ".";
    kvsdir_t dir;
    flux_t f = lua_get_flux (L, 1);

    if (lua_isstring (L, 2)) {
        /*
         *  Format string if path given as > 1 arg:
         */
        if ((lua_gettop (L) > 2) && (l_format_args (L, 2) < 0))
            return (2);
        path = lua_tostring (L, 2);
    }

    if (kvs_get_dir (f, &dir, path) < 0)
        return lua_pusherror (L, strerror (errno));
    return l_push_kvsdir (L, dir);
}

#if 0
static int l_flux_barrier (lua_State *L)
{
    flux_t f = lua_get_flux (L, 1);
    const char *name = luaL_checkstring (L, 2);
    int nprocs = luaL_checkinteger (L, 3);
    return (l_pushresult (L, flux_barrier (f, name, nprocs)));
}
#endif

static int l_flux_rank (lua_State *L)
{
    flux_t f = lua_get_flux (L, 1);
    return (l_pushresult (L, flux_rank (f)));
}

static int l_flux_size (lua_State *L)
{
    flux_t f = lua_get_flux (L, 1);
    return (l_pushresult (L, flux_size (f)));
}

static int l_flux_treeroot (lua_State *L)
{
    flux_t f = lua_get_flux (L, 1);
    lua_pushboolean (L, flux_treeroot (f));
    return (1);
}

static int l_flux_index (lua_State *L)
{
    const char *key = lua_tostring (L, 2);

    if (key == NULL)
        return luaL_error (L, "flux: invalid index");

    if (strcmp (key, "size") == 0)
        return l_flux_size (L);
    if (strcmp (key, "rank") == 0)
        return l_flux_rank (L);
    if (strcmp (key, "treeroot") == 0)
        return l_flux_treeroot (L);

    lua_getmetatable (L, 1);
    lua_getfield (L, -1, key);
    return 1;
}

static int l_flux_send (lua_State *L)
{
    int rc;
    flux_t f = lua_get_flux (L, 1);
    const char *tag = luaL_checkstring (L, 2);
    json_object *o;

    if (lua_value_to_json (L, 3, &o) < 0)
        return lua_pusherror (L, "JSON conversion error");

    if (tag == NULL)
        return lua_pusherror (L, "Invalid args");

    rc = flux_request_send (f, o, tag);
    json_object_put (o);
    if (rc < 0)
        return lua_pusherror (L, strerror (errno));
    return l_pushresult (L, 1);
}

static int l_flux_recv (lua_State *L)
{
    flux_t f = lua_get_flux (L, 1);
    char *tag;
    json_object *o;

    if (flux_response_recv (f, &o, &tag, 0))
        return lua_pusherror (L, strerror (errno));

    json_object_to_lua (L, o);
    json_object_put (o);

    if (tag == NULL)
        return (1);
    lua_pushstring (L, tag);
    return (2);
}

static int l_flux_rpc (lua_State *L)
{
    flux_t f = lua_get_flux (L, 1);
    const char *tag = luaL_checkstring (L, 2);
    json_object *o;
    json_object *resp;

    if (lua_value_to_json (L, 3, &o) < 0)
        return lua_pusherror (L, "JSON conversion error");

    if (tag == NULL || o == NULL)
        return lua_pusherror (L, "Invalid args");

    resp = flux_rpc (f, o, tag);
    json_object_put (o);
    if (resp == NULL)
        return lua_pusherror (L, strerror (errno));

    json_object_to_lua (L, resp);
    json_object_put (resp);
    return (1);
}

static int l_flux_subscribe (lua_State *L)
{
    flux_t f = lua_get_flux (L, 1);

    if (l_format_args (L, 2) < 0)
        return lua_pusherror (L, "Invalid args");

    return l_pushresult (L, flux_event_subscribe (f, lua_tostring (L, 2)));
}

static int l_flux_unsubscribe (lua_State *L)
{
    flux_t f = lua_get_flux (L, 1);

    if (l_format_args (L, 2) < 0)
        return lua_pusherror (L, "Invalid args");

    return l_pushresult (L, flux_event_unsubscribe (f, lua_tostring (L, 2)));
}

static int l_flux_send_event (lua_State *L)
{
    flux_t f = lua_get_flux (L, 1);
    const char *event;
    json_object *o = NULL;
    int rc;
    int eventidx = 2;

    /*
     *  If only 3 or more args were passed then assume json_object
     *   was passed if stack position 2 is a table:
     */
    if ((lua_gettop (L) >= 3) && (lua_istable (L, 2))) {
        eventidx = 3;
        lua_value_to_json (L, 2, &o);
    }

    if ((l_format_args (L, eventidx) < 0))
        return (2); /* nil, err */

    event = luaL_checkstring (L, -1);

    rc = flux_event_send (f, o, event);
    if (o)
        json_object_put (o);

    return l_pushresult (L, rc);
}

static int l_flux_recv_event (lua_State *L)
{
    flux_t f = lua_get_flux (L, 1);
    json_object *o;
    char *tag;

    if (flux_event_recv (f, &o, &tag, 0))
        return lua_pusherror (L, strerror (errno));

    json_object_to_lua (L, o);
    json_object_put (o);

    if (tag == NULL)
        return (1);
    lua_pushstring (L, tag);
    return (2);
}

/*
 *  mrpc
 */
static int lua_push_mrpc (lua_State *L, flux_mrpc_t mrpc)
{
    flux_mrpc_t *mp = lua_newuserdata (L, sizeof (*mp));
    *mp = mrpc;
    luaL_getmetatable (L, "FLUX.mrpc");
    lua_setmetatable (L, -2);
    return (1);
}

static flux_mrpc_t lua_get_mrpc (lua_State *L, int index)
{
    flux_mrpc_t *mp = luaL_checkudata (L, index, "FLUX.mrpc");
    return (*mp);
}

static int l_flux_mrpc_destroy (lua_State *L)
{
    flux_mrpc_t m = lua_get_mrpc (L, 1);
    flux_mrpc_destroy (m);
    return (0);
}

static int l_mrpc_outargs_destroy (lua_State *L)
{
    int *refp = luaL_checkudata (L, 1, "FLUX.mrpc_outarg");
    luaL_unref (L, LUA_REGISTRYINDEX, *refp);
    return (0);
}

static flux_mrpc_t lua_get_mrpc_from_outargs (lua_State *L, int index)
{
    flux_mrpc_t mrpc;
    int *refp = luaL_checkudata (L, index, "FLUX.mrpc_outarg");

    lua_rawgeti (L, LUA_REGISTRYINDEX, *refp);
    mrpc = lua_get_mrpc (L, -1);
    lua_pop (L, 1);
    return mrpc;
}

static int l_mrpc_outargs_iterator (lua_State *L)
{
    int index = lua_upvalueindex (1);
    flux_mrpc_t m = lua_get_mrpc_from_outargs (L, index);
    int n = flux_mrpc_next_outarg (m);
    if (n >= 0) {
        json_object *o;
        if (flux_mrpc_get_outarg (m, n, &o) < 0)
            return lua_pusherror (L, "outarg: %s", strerror (errno));
        lua_pushnumber (L, n);
        json_object_to_lua (L, o);
        json_object_put (o);
        return (2);
    }
    return (0);
}

static int l_mrpc_outargs_next (lua_State *L)
{
    flux_mrpc_t m = lua_get_mrpc_from_outargs (L, 1);
    flux_mrpc_rewind_outarg (m);


    /*
     *  Here we use an iterator closure, but this is probably not
     *   valid since flux mrpc type only allows a single iterator
     *   at a time.
     */
    lua_pushcclosure (L, l_mrpc_outargs_iterator, 1);
    return (1);
}

static int l_mrpc_outargs_index (lua_State *L)
{
    int rc;
    json_object *o;
    flux_mrpc_t m = lua_get_mrpc_from_outargs (L, 1);
    int i;

    if (!lua_isnumber (L, 2)) {
        /* Lookup metatable value */
        lua_getmetatable (L, 1);
        lua_getfield (L, -1, lua_tostring (L, 2));
        return (1);
    }
    /*
     *  Numeric index into individual nodeid outargs
     */
    i = lua_tointeger (L, 2);
    flux_mrpc_get_outarg (m, i, &o);
    rc = json_object_to_lua (L, o);
    json_object_put (o);
    return (rc);
}

static int lua_push_mrpc_outargs (lua_State *L, int index)
{
    /*
     *  Store "outarg" userdata as a reference to the original
     *   mrpc userdata. This averts creatin a new C type for the object
     *   as well as keeping a reference to the original mrpc object
     *   to avoid premature garbage collection.
     */
    int ref;
    int *mref;

    if (!lua_isuserdata (L, index))
        return lua_pusherror (L, "Invalid index when pushing outarg");

    /*  Push userdata at position 'index' to top of stack and take
     *   a reference:
     */
    lua_pushvalue (L, index);
    ref = luaL_ref (L, LUA_REGISTRYINDEX);

    /*  Set our mrpc.outargs "object" to be a container for the reference:
     */
    mref = lua_newuserdata (L, sizeof (int *));
    *mref = ref;
    luaL_getmetatable (L, "FLUX.mrpc_outarg");
    lua_setmetatable (L, -2);

    return (1);
}

static int l_flux_mrpc_index (lua_State *L)
{
    flux_mrpc_t m = lua_get_mrpc (L, 1);
    const char *key = lua_tostring (L, 2);

    if (strcmp (key, "inarg") == 0) {
        json_object *o;

        if (flux_mrpc_get_inarg (m, &o) < 0) {
        fprintf (stderr, "get_inarg: %s\n", strerror (errno));
            return lua_pusherror (L, strerror (errno));
    }

        json_object_to_lua (L, o);
        json_object_put (o);
        return (1);
    }
    if (strcmp (key, "out") == 0) {
        lua_push_mrpc_outargs (L, 1);
        return (1);
    }
    lua_getmetatable (L, 1);
    lua_getfield (L, -1, key);
    return (1);
}

static int l_flux_mrpc_newindex (lua_State *L)
{
    flux_mrpc_t m = lua_get_mrpc (L, 1);
    const char *key = lua_tostring (L, 2);

    if (strcmp (key, "inarg") == 0) {
        json_object *o = NULL;
        if (lua_value_to_json (L, 3, &o) < 0)
            return lua_pusherror (L, "Failed to create json from argument");
        flux_mrpc_put_inarg (m, o);
        json_object_put (o);
        return (0);
    }
    if (strcmp (key, "out") == 0) {
        json_object *o = NULL;
        if (lua_value_to_json (L, 3, &o) < 0)
            return lua_pusherror (L, "Failed to create json from argument");
        flux_mrpc_put_outarg (m, o);
        json_object_put (o);
        return (0);
    }
    return lua_pusherror (L, "Attempt to assign to invalid key mrpc.%s", key);
}

static int l_flux_mrpc_respond (lua_State *L)
{
    return l_pushresult (L, flux_mrpc_respond (lua_get_mrpc (L, 1)));
}

static int l_flux_mrpc_call (lua_State *L)
{
    flux_mrpc_t mrpc = lua_get_mrpc (L, 1);

    if ((l_format_args (L, 2) < 0))
        return (2); /* nil, err */

    return l_pushresult (L, flux_mrpc (mrpc, lua_tostring (L, 2)));
}

static int l_flux_mrpc_new (lua_State *L)
{
    flux_t f = lua_get_flux (L, 1);
    flux_mrpc_t m;

    m = flux_mrpc_create (f, lua_tostring (L, 2));
    if (m == NULL)
        return lua_pusherror (L, "flux_mrpc_create: %s", strerror (errno));

    if (lua_istable (L, 3)) {
        json_object *o;
        lua_value_to_json (L, 3, &o);
        flux_mrpc_put_inarg (m, o);
        json_object_put (o);
    }

    return lua_push_mrpc (L, m);
}

/*
 *  Reactor:
 */
struct l_flux_ref {
    lua_State *L;    /* Copy of this lua state */
    flux_t flux;     /* Copy of flux handle for flux reftable lookup */
    int    ref;      /* reference into flux reftable                 */
};

/*
 *  Convert a table of flux.TYPEMASK to int typemask
 */
static int l_get_typemask (lua_State *L, int index)
{
    int top = lua_gettop (L);
    int t = index < 0 ? top + index + 1 : index;
    int typemask = 0;

    lua_pushnil (L);
    while (lua_next (L, t)) {
        int mask = lua_tointeger (L, -1);
        typemask = typemask | mask;
        lua_pop (L, 1);
    }
    return typemask;
}

void l_flux_ref_destroy (struct l_flux_ref *r, const char *type)
{
    lua_State *L = r->L;
    int top = lua_gettop (L);

    l_get_flux_reftable (L, r->flux);
    lua_getfield (L, -1, type);
    luaL_unref (L, -1, r->ref);
    lua_settop (L, top);
}

struct l_flux_ref *l_flux_ref_create (lua_State *L, flux_t f,
        int index, const char *type)
{
    int ref;
    struct l_flux_ref *mh;
    char metatable [1024];

    /*
     *  Store the table argument at index into the flux.<type> array
     */
    l_get_flux_reftable (L, f);
    lua_getfield (L, -1, type);

    /*
     *  Should have copy of reftable[type] here, o/w create a new table:
     */
    if (lua_isnil (L, -1)) {
        lua_pop (L, 1);             /* pop nil                          */
        lua_newtable (L);           /* new table on top of stack        */
        lua_setfield (L, -2, type); /* set reftable[type] to new table  */
        lua_getfield (L, -1, type); /* put new reftable on top of stack */
    }

    /*  Copy the value at index and return a reference in the retable[type]
     *    table:
     */
    lua_pushvalue (L, index);
    ref = luaL_ref (L, -2);

    /*
     *  Get name for metatable:
     */
    if (snprintf (metatable, sizeof (metatable) - 1, "FLUX.%s", type) < 0)
        return (NULL);

    mh = lua_newuserdata (L, sizeof (*mh));
    luaL_getmetatable (L, metatable);
    lua_setmetatable (L, -2);

    mh->L = L;
    mh->ref = ref;
    mh->flux = f;

    /*
     *  Ensure our userdata object isn't GC'd by tying it to the new
     *   table:
     */
    assert (lua_istable (L, index));
    lua_pushvalue (L, -1); /* Copy it first so it remains at top of stack */
    lua_setfield (L, index, "userdata");

    return (mh);

}

/*
 *  Get the flux reftable of type [name] for the flux_ref object [r]
 */
static int l_flux_ref_gettable (struct l_flux_ref *r, const char *name)
{
    lua_State *L = r->L;
    int top = lua_gettop (L);

    l_get_flux_reftable (L, r->flux);
    lua_getfield (L, -1, name);
    assert (lua_istable (L, -1));

    lua_rawgeti (L, -1, r->ref);
    assert (lua_istable (L, -1));

    lua_replace (L, top+1);
    lua_settop (L, top+1);
    return (1);
}

static int l_f_zi_resp_cb (lua_State *L,
    struct zmsg_info *zi, json_object *resp, void *arg)
{
    flux_t f = arg;
    return l_pushresult (L, flux_respond (f, zmsg_info_zmsg (zi), resp));
}

static int create_and_push_zmsg_info (lua_State *L,
        flux_t f, int typemask, zmsg_t **zmsg)
{
    struct zmsg_info * zi = zmsg_info_create (zmsg, typemask);
    zmsg_info_register_resp_cb (zi, (zi_resp_f) l_f_zi_resp_cb, (void *) f);
    return lua_push_zmsg_info (L, zi);
}

static int msghandler (flux_t f, int typemask, zmsg_t **zmsg, void *arg)
{
    int rc;
    int t;
    struct l_flux_ref *mh = arg;
    lua_State *L = mh->L;

    assert (L != NULL);

    l_flux_ref_gettable (mh, "msghandler");
    t = lua_gettop (L);

    lua_getfield (L, t, "handler");
    assert (lua_isfunction (L, -1));

    lua_push_flux_handle (L, f);
    assert (lua_isuserdata (L, -1));

    create_and_push_zmsg_info (L, f, typemask, zmsg);
    assert (lua_isuserdata (L, -1));

    lua_getfield (L, t, "userdata");
    assert (lua_isuserdata (L, -1));

    if ((rc = lua_pcall (L, 3, 1, 0))) {
        return luaL_error (L, "pcall: %s", lua_tostring (L, -1));
    }

    rc = lua_tonumber (L, -1);

    /* Reset Lua stack */
    lua_settop (L, 0);

    return (rc);
}

static int l_msghandler_remove (lua_State *L)
{
    int t;
    const char *pattern;
    int typemask;
    struct l_flux_ref *mh = luaL_checkudata (L, 1, "FLUX.msghandler");

    l_flux_ref_gettable (mh, "msghandler");
    t = lua_gettop (L);

    lua_getfield (L, t, "pattern");
    pattern = lua_tostring (L, -1);
    lua_getfield (L, t, "msgtypes");
    if (lua_isnil (L, -1))
        typemask = FLUX_MSGTYPE_ANY;
    else
        typemask = l_get_typemask (L, -1);
    /*
     *  Drop reference to the table and allow garbage collection
     */
    flux_msghandler_remove (mh->flux, typemask, pattern);
    l_flux_ref_destroy (mh, "msghandler");
    return (0);
}

static int l_msghandler_add (lua_State *L)
{
    const char *pattern;
    int typemask;
    struct l_flux_ref *mh = NULL;
    flux_t f = lua_get_flux (L, 1);

    if (!lua_istable (L, 2))
        return lua_pusherror (L, "Expected table as 2nd argument");

    /*
     *  Check table for mandatory arguments
     */
    lua_getfield (L, 2, "pattern");
    if (lua_isnil (L, -1))
        return lua_pusherror (L, "Mandatory table argument 'pattern' missing");
    pattern = lua_tostring (L, -1);
    lua_pop (L, 1);

    lua_getfield (L, 2, "handler");
    if (lua_isnil (L, -1))
        return lua_pusherror (L, "Mandatory table argument 'handler' missing");
    lua_pop (L, 1);

    lua_getfield (L, 2, "msgtypes");
    if (lua_isnil (L, -1))
        typemask = FLUX_MSGTYPE_ANY;
    else
        typemask = l_get_typemask (L, -1);
    if (typemask == 0)
        return lua_pusherror (L, "Invalid typemask in msghandler");
    lua_pop (L, 1);

    mh = l_flux_ref_create (L, f, 2, "msghandler");
    flux_msghandler_add (f, typemask, pattern, msghandler, (void *) mh);

    return (1);
}

static int l_msghandler_index (lua_State *L)
{
    struct l_flux_ref *mh = luaL_checkudata (L, 1, "FLUX.msghandler");
    const char *key = lua_tostring (L, 2);

    /*
     *  Check for method names
     */
    if (strcmp (key, "remove") == 0) {
        lua_getmetatable (L, 1);
        lua_getfield (L, -1, "remove");
        return (1);
    }

    /*  Get a copy of the underlying msghandler Lua table and pass-through
     *   the index:
     */
    l_flux_ref_gettable (mh, "msghandler");
    lua_getfield (L, -1, key);
    return (1);
}

static int l_msghandler_newindex (lua_State *L)
{
    struct l_flux_ref *mh = luaL_checkudata (L, 1, "FLUX.msghandler");

    /*  Set value in the underlying msghandler table:
     */
    l_flux_ref_gettable (mh, "msghandler");
    lua_pushvalue (L, 2); /* Key   */
    lua_pushvalue (L, 3); /* Value */
    lua_rawset (L, -3);
    return (0);
}

static int l_kvswatcher (const char *key, json_object *val, void *arg, int errnum)
{
    int rc;
    int t;
    struct l_flux_ref *kw = arg;
    lua_State *L = kw->L;

    assert (L != NULL);
    /* Reset lua stack */
    lua_settop (L, 0);

    l_flux_ref_gettable (kw, "kvswatcher");
    t = lua_gettop (L);

    lua_getfield (L, t, "handler");
    assert (lua_isfunction (L, -1));

    lua_getfield (L, t, "userdata");
    assert (lua_isuserdata (L, -1));

    if (val) {
        json_object_to_lua (L, val);
        lua_pushnil (L);
    }
    else {
        lua_pushnil (L);
        lua_pushnumber (L, errnum);
    }

    if ((rc = lua_pcall (L, 3, 1, 0))) {
        luaL_error (L, "pcall: %s", lua_tostring (L, -1));
    }
    rc = lua_tonumber (L, -1);

    /* Reset stack */
    lua_settop (L, 0);
    return rc;
}

static int l_kvswatcher_remove (lua_State *L)
{
    /* No support for removing kvs watchers */
    lua_pushnil (L);
    lua_pushliteral (L, "Not implemented");
    return (2);
}

static int l_kvswatcher_add (lua_State *L)
{
    struct l_flux_ref *kw = NULL;
    flux_t f = lua_get_flux (L, 1);
    const char *key;

    if (!lua_istable (L, 2))
        return lua_pusherror (L, "Expected table as 2nd argument");

    /*
     *  Check table for mandatory arguments
     */
    lua_getfield (L, 2, "key");
    if (lua_isnil (L, -1))
        return lua_pusherror (L, "Mandatory table argument 'key' missing");
    key = lua_tostring (L, -1);

    lua_getfield (L, 2, "handler");
    if (lua_isnil (L, -1))
        return lua_pusherror (L, "Mandatory table argument 'handler' missing");
    assert (lua_isfunction (L, -1));

    kw = l_flux_ref_create (L, f, 2, "kvswatcher");
    kvs_watch (f, key, l_kvswatcher, (void *) kw);

    return (1);
}

static int l_kvswatcher_index (lua_State *L)
{
    struct l_flux_ref *kw = luaL_checkudata (L, 1, "FLUX.kvswatcher");
    const char *key = lua_tostring (L, 2);

    /*
     *  Check for method names
     */
    if (strcmp (key, "remove") == 0) {
        lua_getmetatable (L, 1);
        lua_getfield (L, -1, "remove");
        return (1);
    }

    /*  Get a copy of the underlying kvswatcher Lua table and pass-through
     *   the index:
     */
    l_flux_ref_gettable (kw, "kvswatcher");
    lua_getfield (L, -1, key);
    return (1);
}

static int l_kvswatcher_newindex (lua_State *L)
{
    struct l_flux_ref *kw = luaL_checkudata (L, 1, "FLUX.kvswatcher");

    /*  Set value in the underlying msghandler table:
     */
    l_flux_ref_gettable (kw, "kvswatcher");
    lua_pushvalue (L, 2); /* Key   */
    lua_pushvalue (L, 3); /* Value */
    lua_rawset (L, -3);
    return (0);
}

static int iowatcher_zio_cb (zio_t zio, json_object *o, void *arg)
{
    int rc;
    int t;
    struct l_flux_ref *iow = arg;
    lua_State *L = iow->L;

    assert (L != NULL);
    lua_settop (L, 0); /* XXX: Reset lua stack so we don't overflow */

    l_flux_ref_gettable (iow, "iowatcher");
    t = lua_gettop (L);

    lua_getfield (L, t, "handler");
    if (!lua_isfunction (L, -1))
        luaL_error (L, "handler is %s not function", luaL_typename (L, -1));

    lua_getfield (L, t, "userdata");
    assert (lua_isuserdata (L, -1));


    if (o) {
        int len;
        uint8_t *pp = NULL;
        util_json_object_get_data (o, "data", &pp, &len);
        if (pp && len > 0) {
            json_object *s = json_object_new_string ((char *)pp);
            json_object_object_add (o, "data", s);
        }
        json_object_to_lua (L, o);
        json_object_put (o);
    }

    rc = lua_pcall (L, 2, 1, 0);
    if (rc)
        fprintf (stderr, "lua_pcall: %s\n", lua_tostring (L, -1));

    return rc ? -1 : 0;
}

static void iowatcher_kz_ready_cb (kz_t kz, void *arg)
{
    int len;
    int t;
    char *data;
    struct l_flux_ref *iow = arg;
    lua_State *L = iow->L;

    assert (L != NULL);

    /* Reset stack so we don't overflow */
    lua_settop (L, 0);

    l_flux_ref_gettable (iow, "iowatcher");
    t = lua_gettop (L);
    lua_getfield (L, t, "handler");
    assert (lua_isfunction (L, -1));

    lua_getfield (L, t, "userdata");
    assert (lua_isuserdata (L, -1));

    while ((len = kz_get (kz, &data)) >= 0) {
        /*
         *  Recreate stack on each iteration:
         */
        lua_pushvalue (L, 2);
        assert (lua_isfunction (L, -1));
        lua_pushvalue (L, 3);
        assert (lua_isuserdata (L, -1));

        if (len > 0) {
            lua_pushlstring (L, data, len);
            free (data);
        }
        if (len == 0)
            lua_pushnil (L);

        if (lua_pcall (L, 2, 1, 0)) {
            fprintf (stderr, "kz_ready: %s\n",  lua_tostring (L, -1));
            break;
        }
        if (len == 0)
            break;
        lua_pop (L, 1);
    }

    lua_settop (L, 0);
}

static int l_iowatcher_add (lua_State *L)
{
    struct l_flux_ref *iow = NULL;
    flux_t f = lua_get_flux (L, 1);

    if (!lua_istable (L, 2))
        return lua_pusherror (L,
            "Expected table, got %s", luaL_typename (L, 2));

    lua_getfield (L, 2, "handler");
    if (lua_isnil (L, -1))
        return lua_pusherror (L, "Mandatory table argument 'handler' missing");
    assert (lua_isfunction (L, -1));

    lua_getfield (L, 2, "fd");
    if (!lua_isnil (L, -1)) {
        zio_t zio;
        int fd = lua_tointeger (L, -1);
        if (fd < 0)
            return lua_pusherror (L, "Invalid fd=%d", fd);
        fd = dup (fd);
        iow = l_flux_ref_create (L, f, 2, "iowatcher");
        zio = zio_reader_create ("", fd, NULL, iow);
        if (!zio)
            fprintf (stderr, "failed to create zio!\n");
        zio_flux_attach (zio, f);
        zio_set_send_cb (zio, (zio_send_f) iowatcher_zio_cb);
    }
    lua_getfield (L, 2, "key");
    if (!lua_isnil (L, -1)) {
        int flags = KZ_FLAGS_READ | KZ_FLAGS_NONBLOCK | KZ_FLAGS_NOEXIST;
        kz_t kz;
        const char *key = lua_tostring (L, -1);
        if ((kz = kz_open (f, key, flags)) == NULL)
            return lua_pusherror (L, "kz_open: %s", strerror (errno));
        iow = l_flux_ref_create (L, f, 2, "iowatcher");
        kz_set_ready_cb (kz, (kz_ready_f) iowatcher_kz_ready_cb, (void *) iow);
    }
    return (1);
}

static int l_iowatcher_index (lua_State *L)
{
    struct l_flux_ref *iow = luaL_checkudata (L, 1, "FLUX.iowatcher");
    const char *key = lua_tostring (L, 2);

    /*
     *  Check for method names
     */
    if (strcmp (key, "remove") == 0) {
        lua_getmetatable (L, 1);
        lua_getfield (L, -1, "remove");
        return (1);
    }

    /*  Get a copy of the underlying kvswatcher Lua table and pass-through
     *   the index:
     */
    l_flux_ref_gettable (iow, "iowatcher");
    lua_getfield (L, -1, key);
    return (1);
}

static int l_iowatcher_newindex (lua_State *L)
{
    struct l_flux_ref *iow = luaL_checkudata (L, 1, "FLUX.iowatcher");

    /*  Set value in the underlying table:
     */
    l_flux_ref_gettable (iow, "iowatcher");
    lua_pushvalue (L, 2); /* Key   */
    lua_pushvalue (L, 3); /* Value */
    lua_rawset (L, -3);
    return (0);
}

static int timeout_handler (flux_t f, void *arg)
{
    int rc;
    int t;
    struct l_flux_ref *to = arg;
    lua_State *L = to->L;

    assert (L != NULL);

    l_flux_ref_gettable (to, "timeout_handler");
    t = lua_gettop (L);

    lua_getfield (L, t, "handler");
    assert (lua_isfunction (L, -1));

    lua_push_flux_handle (L, f);
    assert (lua_isuserdata (L, -1));

    lua_getfield (L, t, "userdata");
    assert (lua_isuserdata (L, -1));

    if ((rc = lua_pcall (L, 2, 1, 0))) {
        return luaL_error (L, "pcall: %s", lua_tostring (L, -1));
    }

    rc = lua_tonumber (L, -1);

    /* Reset Lua stack */
    lua_settop (L, 0);

    return (rc);
}

static int l_timeout_handler_add (lua_State *L)
{
    int id;
    unsigned long ms;
    bool oneshot = true;
    struct l_flux_ref *to = NULL;
    flux_t f = lua_get_flux (L, 1);

    if (!lua_istable (L, 2))
        return lua_pusherror (L, "Expected table as 2nd argument");

    /*
     *  Check table for mandatory arguments
     */
    lua_getfield (L, 2, "timeout");
    if (lua_isnil (L, -1))
        return lua_pusherror (L, "Mandatory table argument 'timeout' missing");
    ms = lua_tointeger (L, -1);
    lua_pop (L, 1);

    lua_getfield (L, 2, "handler");
    if (lua_isnil (L, -1))
        return lua_pusherror (L, "Mandatory table argument 'handler' missing");
    lua_pop (L, 1);

    lua_getfield (L, 2, "oneshot");
    if (!lua_isnil (L, -1))
        oneshot = lua_toboolean (L, -1);
    lua_pop (L, 1);

    to = l_flux_ref_create (L, f, 2, "timeout_handler");
    id = flux_tmouthandler_add (f, ms, oneshot, timeout_handler, (void *) to);
    if (id < 0) {
        l_flux_ref_destroy (to, "timeout_handler");
        return lua_pusherror (L, "flux_tmouthandler_add: %s", strerror (errno));
    }

    /*
     *  Get a copy of the underlying timeout reftable on the stack
     *   and set table.id to the new timer id. This will make the
     *   id available for later callbacks and from lua:
     */
    l_flux_ref_gettable (to, "timeout_handler");
    lua_pushstring (L, "id");
    lua_pushnumber (L, id);
    lua_rawset (L, -3);

    /*
     *  Pop reftable table and leave ref userdata on stack as return value:
     */
    lua_pop (L, 1);

    return (1);
}

static int l_timeout_handler_remove (lua_State *L)
{
    int t;
    int id;
    struct l_flux_ref *to = luaL_checkudata (L, 1, "FLUX.timeout_handler");

    l_flux_ref_gettable (to, "timeout_handler");
    t = lua_gettop (L);

    lua_getfield (L, t, "id");
    id = lua_tointeger (L, -1);
    /*
     *  Drop reference to the table and allow garbage collection
     */
    flux_tmouthandler_remove (to->flux, id);
    l_flux_ref_destroy (to, "timeout_handler");
    return (0);
}

static int l_timeout_handler_index (lua_State *L)
{
    struct l_flux_ref *to = luaL_checkudata (L, 1, "FLUX.timeout_handler");
    const char *key = lua_tostring (L, 2);

    /*
     *  Check for method names
     */
    if (strcmp (key, "remove") == 0) {
        lua_getmetatable (L, 1);
        lua_getfield (L, -1, "remove");
        return (1);
    }

    /*  Get a copy of the underlying timeout handler Lua table and pass-through
     *   the index:
     */
    l_flux_ref_gettable (to, "timeout_handler");
    lua_getfield (L, -1, key);
    return (1);
}

static int l_timeout_handler_newindex (lua_State *L)
{
    struct l_flux_ref *to = luaL_checkudata (L, 1, "FLUX.timeout_handler");

    /*  Set value in the underlying msghandler table:
     */
    l_flux_ref_gettable (to, "timeout_handler");
    lua_pushvalue (L, 2); /* Key   */
    lua_pushvalue (L, 3); /* Value */
    lua_rawset (L, -3);
    return (0);
}

static int l_flux_reactor_start (lua_State *L)
{
    return l_pushresult (L, flux_reactor_start (lua_get_flux (L, 1)));
}

static int l_flux_reactor_stop (lua_State *L)
{
    flux_reactor_stop (lua_get_flux (L, 1));
    return 0;
}

static int lua_push_kz (lua_State *L, kz_t kz)
{
    kz_t *kzp = lua_newuserdata (L, sizeof (*kzp));
    *kzp = kz;
    luaL_getmetatable (L, "FLUX.kz");
    lua_setmetatable (L, -2);
    return (1);
}

static int l_flux_kz_open (lua_State *L)
{
    kz_t kz;
    flux_t f = lua_get_flux (L, 1);
    const char *key = lua_tostring (L, 2);
    const char *mode = lua_tostring (L, 3);
    int flags;

    if (mode[0] == 'r')
        flags = KZ_FLAGS_READ | KZ_FLAGS_NOEXIST | KZ_FLAGS_NONBLOCK;
    else if (mode[0] == 'w')
        flags = KZ_FLAGS_WRITE;
    else
        return lua_pusherror (L, "Expected 'r' or 'w' mode for kz_open");

    kz = kz_open (f, key, flags);
    return lua_push_kz (L, kz);
}

static kz_t lua_get_kz (lua_State *L, int index)
{
    kz_t *kzp = luaL_checkudata (L, index, "FLUX.kz");
    return (*kzp);
}

static int l_kz_index (lua_State *L)
{
    const char *key = lua_tostring (L, 2);

    lua_getmetatable (L, 1);
    lua_getfield (L, -1, key);
    return (1);
}

static int l_kz_gc (lua_State *L)
{
    kz_t *kzp = luaL_checkudata (L, 1, "FLUX.kz");
    if (*kzp != NULL)
        kz_close (*kzp);
    return (0);
}

static int l_kz_close (lua_State *L)
{
    kz_t *kzp = luaL_checkudata (L, 1, "FLUX.kz");
    kz_close (*kzp);
    *kzp = NULL;
    return (0);
}

static int l_kz_write (lua_State *L)
{
    kz_t kz = lua_get_kz (L, 1);
    size_t len;
    const char *s = lua_tolstring (L, 2, &len);

    if (kz_put (kz, (char *) s, len) < 0)
        return lua_pusherror (L, strerror (errno));
    return (1); /* len */
}

static const struct luaL_Reg flux_functions [] = {
    { "new",             l_flux_new         },
    { NULL,              NULL              }
};

static const struct luaL_Reg flux_methods [] = {
    { "__gc",            l_flux_destroy     },
    { "__index",         l_flux_index       },
    { "kvsdir",          l_flux_kvsdir_new  },
//    { "barrier",         l_flux_barrier     },
    { "send",            l_flux_send        },
    { "recv",            l_flux_recv        },
    { "rpc",             l_flux_rpc         },
    { "mrpc",            l_flux_mrpc_new    },
    { "sendevent",       l_flux_send_event  },
    { "recv_event",      l_flux_recv_event },
    { "subscribe",       l_flux_subscribe   },
    { "unsubscribe",     l_flux_unsubscribe },
    { "kz_open",         l_flux_kz_open     },
    { "msghandler",      l_msghandler_add    },
    { "kvswatcher",      l_kvswatcher_add    },
    { "iowatcher",       l_iowatcher_add     },
    { "timer",           l_timeout_handler_add },
    { "reactor",         l_flux_reactor_start },
    { "reactor_stop",    l_flux_reactor_stop },
    { NULL,              NULL               }
};

static const struct luaL_Reg mrpc_methods [] = {
    { "__gc",            l_flux_mrpc_destroy  },
    { "__index",         l_flux_mrpc_index    },
    { "__newindex",      l_flux_mrpc_newindex },
    { "__call",          l_flux_mrpc_call     },
    { "respond",         l_flux_mrpc_respond  },
    { NULL,              NULL                 }
};

static const struct luaL_Reg mrpc_outargs_methods [] = {
    { "__gc",            l_mrpc_outargs_destroy },
    { "__index",         l_mrpc_outargs_index   },
    { "next",            l_mrpc_outargs_next    },
    { NULL,              NULL                        }
};

static const struct luaL_Reg msghandler_methods [] = {
    { "__index",         l_msghandler_index    },
    { "__newindex",      l_msghandler_newindex },
    { "remove",          l_msghandler_remove   },
    { NULL,              NULL                  }
};

static const struct luaL_Reg kvswatcher_methods [] = {
    { "__index",         l_kvswatcher_index    },
    { "__newindex",      l_kvswatcher_newindex },
    { "remove",          l_kvswatcher_remove   },
    { NULL,              NULL                  }
};

static const struct luaL_Reg iowatcher_methods [] = {
    { "__index",         l_iowatcher_index    },
    { "__newindex",      l_iowatcher_newindex },
    { NULL,              NULL                  }
};

static const struct luaL_Reg kz_methods [] = {
    { "__index",         l_kz_index           },
    { "__gc",            l_kz_gc              },
    { "close",           l_kz_close           },
    { "write",           l_kz_write           },
    { NULL,              NULL                 }
};

static const struct luaL_Reg timeout_handler_methods [] = {
    { "__index",         l_timeout_handler_index    },
    { "__newindex",      l_timeout_handler_newindex },
    { "remove",          l_timeout_handler_remove   },
    { NULL,              NULL                  }
};

#define MSGTYPE_SET(L, name) do { \
  lua_pushlstring(L, #name, sizeof(#name)-1); \
  lua_pushnumber(L, FLUX_ ## name); \
  lua_settable(L, -3); \
} while (0);


int luaopen_flux (lua_State *L)
{
    luaL_newmetatable (L, "FLUX.mrpc");
    luaL_setfuncs (L, mrpc_methods, 0);
    luaL_newmetatable (L, "FLUX.mrpc_outarg");
    luaL_setfuncs (L, mrpc_outargs_methods, 0);
    luaL_newmetatable (L, "FLUX.msghandler");
    luaL_setfuncs (L, msghandler_methods, 0);
    luaL_newmetatable (L, "FLUX.kvswatcher");
    luaL_setfuncs (L, kvswatcher_methods, 0);
    luaL_newmetatable (L, "FLUX.iowatcher");
    luaL_setfuncs (L, iowatcher_methods, 0);
    luaL_newmetatable (L, "FLUX.kz");
    luaL_setfuncs (L, kz_methods, 0);
    luaL_newmetatable (L, "FLUX.timeout_handler");
    luaL_setfuncs (L, timeout_handler_methods, 0);

    luaL_newmetatable (L, "FLUX.handle");
    luaL_setfuncs (L, flux_methods, 0);
    /*
     * Load required kvs library
     */
    luaopen_kvs (L);
    l_zmsg_info_register_metatable (L);
    lua_newtable (L);
    luaL_setfuncs (L, flux_functions, 0);

    MSGTYPE_SET (L, MSGTYPE_REQUEST);
    MSGTYPE_SET (L, MSGTYPE_RESPONSE);
    MSGTYPE_SET (L, MSGTYPE_EVENT);
    MSGTYPE_SET (L, MSGTYPE_ANY);

    lua_push_json_null (L);
    lua_pushliteral (L, "NULL");
    lua_settable (L, -3);

    return (1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
