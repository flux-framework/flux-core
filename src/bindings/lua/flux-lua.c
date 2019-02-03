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
#include "config.h"
#endif
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include <sys/types.h>

#include <lua.h>
#include <lauxlib.h>

#include "flux/core.h"

#include "src/common/libcompat/reactor.h"

#include "jansson-lua.h"
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
static int l_get_flux_reftable (lua_State *L, flux_t *f)
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
    }

    return (1);
}

/*
 *  When we push a flux_t handle [f], we first check to see if
 *   there is an existing flux reftable, and if so we just return
 *   a reference to the existing object. Otherwise, create the reftable
 *   and return the new object.
 */
static int lua_push_flux_handle (lua_State *L, flux_t *f)
{
    flux_t **fp;
    int top = lua_gettop (L);

    /*
     *  First see if this flux_t object already has a lua component:
     */
    l_get_flux_reftable (L, f);  /* [ reftable, ... ] */
    lua_pushliteral (L, "flux"); /* [ flux, reftable, ... ] */
    lua_rawget (L, -2);          /* [ value, reftable, ... ] */
    if (lua_istable (L, -1)) {
        lua_rawgeti (L, -1, 1);  /* [ userdata, table, reftable, ... ] */

        if (lua_isuserdata (L, -1)) {
            /* Restore stack with userdata on top */
            lua_replace (L, top+1);  /* [ table, userdata, ... ] */
            lua_settop (L, top+1);   /* [ userdata, .... ] */
            return (1);
        }
        /* If we didn't find a userdata (partial initialization?)
         *  then we'll have to recreate it. First pop top of stack,
         *  and continue:
         */
        lua_pop (L, 1);
    }

    lua_settop (L, top); /* Reset stack */
    /*
     *  Otherwise create a new Lua object:
     *
     *  1. Store pointer to this flux_t handle in a userdata:
     */
    fp = lua_newuserdata (L, sizeof (*fp));
    *fp = f;

    /*
     *  2. Set metatable for Lua "flux" object so it inherets the right
     *     methods:
     */
    luaL_getmetatable (L, "FLUX.handle");
    lua_setmetatable (L, -2);

    /*
     *  3. Store a reference table containing this object with weak keys
     *     so we don't hold a reference.
     */

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

    /* Return userdata as flux object */
    return (1);
}

int lua_push_flux_handle_external (lua_State *L, flux_t *f)
{
    /*
     *  Increase reference count on this flux handle since we are
     *   pushing a handle opened external into Lua. We will rely on
     *   lua gc to decref via flux_close().
     */
    flux_incref (f);
    return (lua_push_flux_handle (L, f));
}

static void l_flux_reftable_unref (lua_State *L, flux_t *f)
{
    l_get_flux_reftable (L, f);
    if (lua_istable (L, -1)) {
        lua_pushliteral (L, "flux");
        lua_pushnil (L);
        lua_rawset (L, -3);
    }
}

static flux_t *lua_get_flux (lua_State *L, int index)
{
    flux_t **fluxp = luaL_checkudata (L, index, "FLUX.handle");
    return (*fluxp);
}

static int l_flux_destroy (lua_State *L)
{
    flux_t *f = lua_get_flux (L, 1);
    l_flux_reftable_unref (L, f);
    flux_close (f);
    return (0);
}

static int l_flux_new (lua_State *L)
{
    const char *s = NULL;
    flux_t *f;
    if (lua_isstring (L, 1))
        s = lua_tostring (L, 1);
    f = flux_open (s, 0);
    if (f == NULL)
        return lua_pusherror (L, (char *)flux_strerror (errno));
    return (lua_push_flux_handle (L, f));
}

static int l_flux_kvsdir_new (lua_State *L)
{
    const char *path = ".";
    const flux_kvsdir_t *dir;
    flux_kvsdir_t *cpy;
    flux_t *f = lua_get_flux (L, 1);
    flux_future_t *fut = NULL;
    int rc;

    if (lua_isstring (L, 2)) {
        /*
         *  Format string if path given as > 1 arg:
         */
        if ((lua_gettop (L) > 2) && (l_format_args (L, 2) < 0))
            return (2);
        path = lua_tostring (L, 2);
    }
    if (!(fut = flux_kvs_lookup (f, NULL, FLUX_KVS_READDIR, path))
            || flux_kvs_lookup_get_dir (fut, &dir) < 0
            || !(cpy = flux_kvsdir_copy (dir))) {
        rc = lua_pusherror (L, (char *)flux_strerror (errno));
        goto done;
    }
    rc = lua_push_kvsdir (L, cpy);
done:
    flux_future_destroy (fut);
    return rc;
}

static int l_flux_kvs_symlink (lua_State *L)
{
    flux_t *f;
    const char *key;
    const char *target;

    if (!(f = lua_get_flux (L, 1)))
        return lua_pusherror (L, "flux handle expected");
    if (!(key = lua_tostring (L, 2)))
        return lua_pusherror (L, "key expected in arg #2");
    if (!(target = lua_tostring (L, 3)))
        return lua_pusherror (L, "target expected in arg #3");

    if (flux_kvs_symlink (f, key, target) < 0)
        return lua_pusherror (L, (char *)flux_strerror (errno));
    lua_pushboolean (L, true);
    return (1);
}

static int l_flux_kvs_unlink (lua_State *L)
{
    flux_t *f;
    const char *key;
    if (!(f = lua_get_flux (L, 1)))
        return lua_pusherror (L, "flux handle expected");
    if (!(key = lua_tostring (L, 2)))
        return lua_pusherror (L, "key expected in arg #2");

    if (flux_kvs_unlink (f, key) < 0)
        return lua_pusherror (L, (char *)flux_strerror (errno));
    lua_pushboolean (L, true);
    return (1);
}

static int l_flux_kvs_type (lua_State *L)
{
    flux_t *f;
    const char *key;
    const flux_kvsdir_t *dir;
    const char *json_str;
    flux_kvsdir_t *cpy;
    flux_future_t *future;
    const char *target;

    if (!(f = lua_get_flux (L, 1)))
        return lua_pusherror (L, "flux handle expected");
    if (!(key = lua_tostring (L, 2)))
        return lua_pusherror (L, "key expected in arg #2");

    if ((future = flux_kvs_lookup (f, NULL, FLUX_KVS_READLINK, key))
            && flux_kvs_lookup_get_symlink (future, NULL, &target) == 0) {
        lua_pushstring (L, "symlink");
        lua_pushstring (L, target);
        flux_future_destroy (future);
        return (2);
    }
    flux_future_destroy (future);
    if ((future = flux_kvs_lookup (f, NULL, FLUX_KVS_READDIR, key))
            && flux_kvs_lookup_get_dir (future, &dir) == 0
            && (cpy = flux_kvsdir_copy (dir))) {
        lua_pushstring (L, "dir");
        lua_push_kvsdir (L, cpy);
        flux_future_destroy (future);
        return (2);
    }
    flux_future_destroy (future);
    if ((future = flux_kvs_lookup (f, NULL, 0, key))
            && flux_kvs_lookup_get (future, &json_str) == 0) {
        lua_pushstring (L, "file");
        if (!json_str || json_object_string_to_lua (L, json_str) < 0)
            lua_pushnil (L);
        flux_future_destroy (future);
        return (2);
    }
    flux_future_destroy (future);
    return lua_pusherror (L, "key does not exist");
}

int l_flux_kvs_commit (lua_State *L)
{
    flux_t *f = lua_get_flux (L, 1);
    if (flux_kvs_commit_anon (f, 0) < 0)
         return lua_pusherror (L, (char *)flux_strerror (errno));
    lua_pushboolean (L, true);
    return (1);
}

int l_flux_kvs_put (lua_State *L)
{
    int rc;
    flux_t *f = lua_get_flux (L, 1);
    const char *key = lua_tostring (L, 2);
    if (key == NULL)
        return lua_pusherror (L, "key required");

    if (lua_isnil (L, 3))
        rc = flux_kvs_put (f, key, NULL);
    else {
        char *json_str = NULL;
        if (lua_value_to_json_string (L, 3, &json_str) < 0)
            return lua_pusherror (L, "Unable to convert to json");
        rc = flux_kvs_put (f, key, json_str);
        free (json_str);
    }
    if (rc < 0)
        return lua_pusherror (L, "flux_kvs_put (%s): %s",
                                key, (char *)flux_strerror (errno));

    lua_pushboolean (L, true);
    return (1);
}

int l_flux_kvs_get (lua_State *L)
{
    flux_future_t *fut = NULL;
    const char *json_str;
    flux_t *f = lua_get_flux (L, 1);
    const char *key = lua_tostring (L, 2);
    int rc;

    if (key == NULL) {
        rc = lua_pusherror (L, "key required");
        goto done;
    }
    if (!(fut = flux_kvs_lookup (f, NULL, 0, key))
            || flux_kvs_lookup_get (fut, &json_str) < 0) {
        rc = lua_pusherror (L, "flux_kvs_lookup: %s",
                              (char *)flux_strerror (errno));
        goto done;
    }
    if (json_object_string_to_lua (L, json_str) < 0) {
        rc = lua_pusherror (L, "JSON decode error: %s",
                              (char *)flux_strerror (errno));
        goto done;
    }
    rc = 1;
done:
    flux_future_destroy (fut);
    return (rc);
}

static int l_flux_barrier (lua_State *L)
{
    flux_t *f = lua_get_flux (L, 1);
    const char *name = luaL_checkstring (L, 2);
    int nprocs = luaL_checkinteger (L, 3);
    flux_future_t *future = flux_barrier (f, name, nprocs);
    int rc = future ? flux_future_get (future , NULL) : -1;
    flux_future_destroy (future);
    return (l_pushresult (L, rc));
}

static int l_flux_rank (lua_State *L)
{
    flux_t *f = lua_get_flux (L, 1);
    uint32_t rank;
    if (flux_get_rank (f, &rank) < 0)
        return lua_pusherror (L, "flux_get_rank error");
    return (l_pushresult (L, rank));
}

static int l_flux_size (lua_State *L)
{
    flux_t *f = lua_get_flux (L, 1);
    uint32_t size;
    if (flux_get_size (f, &size) < 0)
        return lua_pusherror (L, "flux_get_size error");
    return (l_pushresult (L, size));
}

static int l_flux_arity (lua_State *L)
{
    flux_t *f = lua_get_flux (L, 1);
    const char *s;
    int arity;

    if (!(s = flux_attr_get (f, "tbon.arity")))
        return lua_pusherror (L, "flux_attr_get tbon.arity error");
    arity = strtoul (s, NULL, 10);
    return (l_pushresult (L, arity));
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
    if (strcmp (key, "arity") == 0)
        return l_flux_arity (L);

    lua_getmetatable (L, 1);
    lua_getfield (L, -1, key);
    return 1;
}

static int send_json_request (flux_t *h, uint32_t nodeid, uint32_t matchtag,
                              const char *topic, const char *json_str)
{
    flux_msg_t *msg;
    int msgflags = 0;
    int rc = -1;

    if (!(msg = flux_request_encode (topic, json_str)))
        goto done;
    if (flux_msg_set_matchtag (msg, matchtag) < 0)
        goto done;
    if (nodeid == FLUX_NODEID_UPSTREAM) {
        msgflags |= FLUX_MSGFLAG_UPSTREAM;
        if (flux_get_rank (h, &nodeid) < 0)
            goto done;
    }
    if (flux_msg_set_nodeid (msg, nodeid, msgflags) < 0)
        goto done;
    if (flux_send (h, msg, 0) < 0)
        goto done;
    rc = 0;
done:
    flux_msg_destroy (msg);
    return rc;
}

static int l_flux_send (lua_State *L)
{
    int rc;
    int nargs = lua_gettop (L) - 1;
    flux_t *f = lua_get_flux (L, 1);
    const char *tag = luaL_checkstring (L, 2);
    char *json_str = NULL;
    uint32_t nodeid = FLUX_NODEID_ANY;
    uint32_t matchtag;

    if (lua_value_to_json_string (L, 3, &json_str) < 0)
        return lua_pusherror (L, "JSON conversion error");

    if (tag == NULL)
        return lua_pusherror (L, "Invalid args");

    if (nargs >= 3)
        nodeid = lua_tointeger (L, 4);

    matchtag = flux_matchtag_alloc (f, 0);
    if (matchtag == FLUX_MATCHTAG_NONE)
        return lua_pusherror (L, (char *)flux_strerror (errno));

    rc = send_json_request (f, nodeid, matchtag, tag, json_str);
    free (json_str);
    if (rc < 0)
        return lua_pusherror (L, (char *)flux_strerror (errno));

    return l_pushresult (L, matchtag);
}

static int l_flux_recv (lua_State *L)
{
    flux_t *f = lua_get_flux (L, 1);
    const char *topic = NULL;
    const char *json_str = NULL;
    int errnum;
    flux_msg_t *msg;
    struct flux_match match = {
        .typemask = FLUX_MSGTYPE_RESPONSE,
        .matchtag = FLUX_MATCHTAG_NONE,
        .topic_glob = NULL,
    };

    if (lua_gettop (L) > 1)
        match.matchtag = lua_tointeger (L, 2);

    if (!(msg = flux_recv (f, match, 0)))
        goto error;

    if (flux_msg_get_errnum (msg, &errnum) < 0)
        goto error;

    if (errnum == 0 && (flux_msg_get_topic (msg, &topic) < 0
                     || flux_msg_get_string (msg, &json_str) < 0))
        goto error;

    if (json_str)
        json_object_string_to_lua (L, json_str);
    else {
        lua_newtable (L);
    }

    /* XXX: Backwards compat code, remove someday:
     *  Promote errnum, if nonzero, into table on stack
     */
    if (errnum != 0) {
        lua_pushnumber (L, errnum);
        lua_setfield (L, -1, "errnum");
    }

    if (topic)
        lua_pushstring (L, topic);
    else
        lua_pushnil (L);
    return (2);
error:
    if (msg) {
        int saved_errno = errno;
        flux_msg_destroy (msg);
        errno = saved_errno;
    }
    return lua_pusherror (L, (char *)flux_strerror (errno));
}

static int l_flux_rpc (lua_State *L)
{
    flux_t *f = lua_get_flux (L, 1);
    const char *tag = luaL_checkstring (L, 2);
    int nodeid;
    flux_future_t *fut = NULL;
    char *json_str;
    const char *s = NULL;
    int rc;

    if (lua_value_to_json_string (L, 3, &json_str) < 0) {
        rc = lua_pusherror (L, "JSON conversion error");
        goto done;
    }

    if (lua_gettop (L) > 3)
        nodeid = lua_tonumber (L, 4);
    else
        nodeid = FLUX_NODEID_ANY;

    if (tag == NULL || json_str == NULL) {
        rc = lua_pusherror (L, "Invalid args");
        goto done;
    }
    /* flux_rpc() no longer checks that string payload is a JSON object.
     * Do that here since we know the payload here is intended to be JSON,
     * therefore should be an object not an array per RFC 3.
     */
    if (json_str[0] != '{' || json_str[strlen (json_str) - 1] != '}') {
        errno = EINVAL;
        rc = lua_pusherror (L, (char *)flux_strerror (errno));
        goto done;
    }
    fut = flux_rpc (f, tag, json_str, nodeid, 0);
    free (json_str);
    if (!fut || flux_rpc_get (fut, &s) < 0) {
        rc = lua_pusherror (L, (char *)flux_strerror (errno));
        goto done;
    }
    if (json_object_string_to_lua (L, s ? : "{}") < 0) {
        rc = lua_pusherror (L, "response JSON conversion error");
        goto done;
    }
    rc = 1;
done:
    flux_future_destroy (fut);
    return (rc);
}

static int l_flux_getattr (lua_State *L)
{
    flux_t *f = lua_get_flux (L, 1);
    const char *name = luaL_checkstring (L, 2);
    const char *val = flux_attr_get (f, name);
    if (val == NULL)
        return lua_pusherror (L, (char *)flux_strerror (errno));
    lua_pushstring (L, val);
    return (1);
}

static int l_flux_subscribe (lua_State *L)
{
    flux_t *f = lua_get_flux (L, 1);

    if (l_format_args (L, 2) < 0)
        return lua_pusherror (L, "Invalid args");

    return l_pushresult (L, flux_event_subscribe (f, lua_tostring (L, 2)));
}

static int l_flux_unsubscribe (lua_State *L)
{
    flux_t *f = lua_get_flux (L, 1);

    if (l_format_args (L, 2) < 0)
        return lua_pusherror (L, "Invalid args");

    return l_pushresult (L, flux_event_unsubscribe (f, lua_tostring (L, 2)));
}

static int l_flux_send_event (lua_State *L)
{
    flux_t *f = lua_get_flux (L, 1);
    const char *event;
    char *json_str = NULL;
    int eventidx = 2;
    flux_msg_t *msg;
    int rc = 0;

    /*
     *  If only 3 or more args were passed then assume json_object
     *   was passed if stack position 2 is a table:
     */
    if ((lua_gettop (L) >= 3) && (lua_istable (L, 2))) {
        eventidx = 3;
        lua_value_to_json_string (L, 2, &json_str);
    }

    if ((l_format_args (L, eventidx) < 0))
        return (2); /* nil, err */

    event = luaL_checkstring (L, -1);

    msg = flux_event_encode (event, json_str);
    free (json_str);

    if (!msg || flux_send (f, msg, 0) < 0)
        rc = -1;
    flux_msg_destroy (msg);
    return l_pushresult (L, rc);
}

static int l_flux_recv_event (lua_State *L)
{
    flux_t *f = lua_get_flux (L, 1);
    const char *json_str = NULL;
    const char *topic;
    struct flux_match match = {
        .typemask = FLUX_MSGTYPE_EVENT,
        .matchtag = FLUX_MATCHTAG_NONE,
        .topic_glob = NULL,
    };
    flux_msg_t *msg = NULL;

    if (!(msg = flux_recv (f, match, 0)))
        return lua_pusherror (L, (char *)flux_strerror (errno));

    if (flux_event_decode (msg, &topic, &json_str) < 0) {
        flux_msg_destroy (msg);
        return lua_pusherror (L, (char *)flux_strerror (errno));
    }

    /* FIXME: create empty JSON object if message payload was empty,
     * because flux_sendmsg () previously ensured the payload was never
     * empty, and t/lua/t0003-events.t (tests 19 and 20) will fail if this
     * isn't so.  Need to revisit that test, and find any dependencies on
     * this invariant in the lua code.
     */
    json_object_string_to_lua (L, json_str ? json_str : "{}");
    lua_pushstring (L, topic);
    flux_msg_destroy (msg);
    return (2);
}

/*
 *  Reactor:
 */
struct l_flux_ref {
    lua_State *L;    /* Copy of this lua state */
    flux_t *flux;     /* Copy of flux handle for flux reftable lookup */
    void   *arg;     /* optional argument */
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

struct l_flux_ref *l_flux_ref_create (lua_State *L, flux_t *f,
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
    mh->arg = NULL;

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
    struct zmsg_info *zi, const char *json_str, void *arg)
{
    flux_t *f = arg;
    flux_msg_t **msgp = zmsg_info_zmsg (zi);
    int rc;
    if ((rc = flux_respond (f, *msgp, 0, json_str)) == 0) {
        flux_msg_destroy (*msgp);
        *msgp = NULL;
    }
    return l_pushresult (L, rc);
}

static int create_and_push_zmsg_info (lua_State *L,
        flux_t *f, int typemask, flux_msg_t **msg)
{
    struct zmsg_info * zi = zmsg_info_create (msg, typemask);
    zmsg_info_register_resp_cb (zi, (zi_resp_f) l_f_zi_resp_cb, (void *) f);
    return lua_push_zmsg_info (L, zi);
}

static int l_flux_recvmsg (lua_State *L)
{
    flux_t *f = lua_get_flux (L, 1);
    flux_msg_t *msg;
    int type;
    struct flux_match match = FLUX_MATCH_ANY;

    if (lua_gettop (L) > 1)
        match.matchtag = lua_tointeger (L, 2);

    if (!(msg = flux_recv (f, match, 0)))
        return lua_pusherror (L, (char *)flux_strerror (errno));

    if (flux_msg_get_type (msg, &type) < 0)
        type = FLUX_MSGTYPE_ANY;

    create_and_push_zmsg_info (L, f, type, &msg);
    flux_msg_destroy (msg);
    return (1);
}

static int msghandler (flux_t *f, int typemask, flux_msg_t **msg, void *arg)
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

    create_and_push_zmsg_info (L, f, typemask, msg);
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
    flux_t *f = lua_get_flux (L, 1);

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
    if (flux_msghandler_add (f, typemask, pattern, msghandler, (void *) mh) < 0) {
        l_flux_ref_destroy (mh, "msghandler");
        return lua_pusherror (L, "flux_msghandler_add: %s",
                             (char *)flux_strerror (errno));
    }

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

void push_stat_table (lua_State *L, struct stat *s)
{
    int t;
    lua_newtable (L);
    t = lua_gettop (L);
    lua_pushinteger (L, s->st_dev);
    lua_setfield (L, t, "st_dev");
    lua_pushinteger (L, s->st_ino);
    lua_setfield (L, t, "st_ino");
    lua_pushinteger (L, s->st_mode);
    lua_setfield (L, t, "st_mode");
    lua_pushinteger (L, s->st_nlink);
    lua_setfield (L, t, "st_nlink");
    lua_pushinteger (L, s->st_uid);
    lua_setfield (L, t, "st_uid");
    lua_pushinteger (L, s->st_gid);
    lua_setfield (L, t, "st_gid");
    lua_pushinteger (L, s->st_size);
    lua_setfield (L, t, "st_size");
    lua_pushinteger (L, s->st_atime);
    lua_setfield (L, t, "st_atime");
    lua_pushinteger (L, s->st_mtime);
    lua_setfield (L, t, "st_mtime");
    lua_pushinteger (L, s->st_ctime);
    lua_setfield (L, t, "st_ctime");
    lua_pushinteger (L, s->st_blksize);
    lua_setfield (L, t, "st_blksize");
    lua_pushinteger (L, s->st_blocks);
    lua_setfield (L, t, "st_blocks");
}

void stat_watcher_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    struct stat st, prev;
    int rc, t;
    struct l_flux_ref *sw = arg;
    lua_State *L = sw->L;

    flux_stat_watcher_get_rstat (w, &st, &prev);

    l_flux_ref_gettable (sw, "watcher");
    t = lua_gettop (L);

    lua_getfield (L, t, "handler");
    lua_getfield (L, t, "userdata");
    push_stat_table (L, &st);
    push_stat_table (L, &prev);

    if ((rc = lua_pcall (L, 3, 1, 0))) {
        luaL_error (L, "stat_watcher: pcall: %s", lua_tostring (L, -1));
        return;
    }
}

static int l_stat_watcher_add (lua_State *L)
{
    flux_watcher_t *w;
    struct l_flux_ref *sw = NULL;
    const char *path;
    double interval = 0.0;
    flux_t *f = lua_get_flux (L, 1);

    if (!lua_istable (L, 2))
        return lua_pusherror (L, "Expected table as 2nd argument");

    lua_getfield (L, 2, "path");
    if (lua_isnil (L, -1))
        return lua_pusherror (L, "Mandatory argument 'path' missing");
    path = lua_tostring (L, -1);
    lua_pop (L, 1);

    lua_getfield (L, 2, "interval");
    if (lua_isnumber (L, -1))
        interval = lua_tonumber (L, -1);
    lua_pop (L, 1);

    lua_getfield (L, 2, "handler");
    if (lua_isnil (L, -1))
        return lua_pusherror (L, "Mandatory table argument 'handler' missing");
    lua_pop (L, 1);

    sw = l_flux_ref_create (L, f, 2, "watcher");
    w = flux_stat_watcher_create (flux_get_reactor (f), path, interval,
                                  stat_watcher_cb, (void *) sw);
    sw->arg = w;
    if (w == NULL) {
        l_flux_ref_destroy (sw, "watcher");
        return lua_pusherror (L, "flux_stat_watcher_create: %s",
                                 (char *)flux_strerror (errno));
    }

    flux_watcher_start (w);
    return (1);
}

static int l_watcher_destroy (lua_State *L)
{
    struct l_flux_ref *fw = luaL_checkudata (L, 1, "FLUX.watcher");
    flux_watcher_t *w = fw->arg;
    if (w) {
        flux_watcher_destroy (w);
        fw->arg = NULL;
    }
    return (0);
}

static int l_watcher_remove (lua_State *L)
{
    struct l_flux_ref *fw = luaL_checkudata (L, 1, "FLUX.watcher");
    flux_watcher_t *w = fw->arg;
    flux_watcher_stop (w);
    lua_pushboolean (L, true);
    return (1);
}

static int l_watcher_index (lua_State *L)
{
    struct l_flux_ref *fw = luaL_checkudata (L, 1, "FLUX.watcher");
    const char *key = lua_tostring (L, 2);

    /*
     *  Check for method names
     */
    if (strcmp (key, "remove") == 0) {
        lua_getmetatable (L, 1);
        lua_getfield (L, -1, "remove");
        return (1);
    }

    /*  Get a copy of the underlying watcher Lua table and pass-through
     *   the index:
     */
    l_flux_ref_gettable (fw, "watcher");
    lua_getfield (L, -1, key);
    return (1);
}

static int l_watcher_newindex (lua_State *L)
{
    struct l_flux_ref *iow = luaL_checkudata (L, 1, "FLUX.watcher");

    /*  Set value in the underlying table:
     */
    l_flux_ref_gettable (iow, "watcher");
    lua_pushvalue (L, 2); /* Key   */
    lua_pushvalue (L, 3); /* Value */
    lua_rawset (L, -3);
    return (0);
}


static int timeout_handler (flux_t *f, void *arg)
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
    flux_t *f = lua_get_flux (L, 1);

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
        return lua_pusherror (L, "flux_tmouthandler_add: %s",
                              (char *)flux_strerror (errno));
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
    int rc;
    const char *arg;
    const char *reason;
    flux_t *h;
    int mode = 0;
    if ((lua_gettop (L) > 1) && (arg = lua_tostring (L, 2))) {
        if (strcmp (arg, "once") == 0)
            mode = FLUX_REACTOR_ONCE;
        else if (strcmp (arg, "nowait") == 0)
            mode = FLUX_REACTOR_NOWAIT;
        else
            return lua_pusherror (L, "flux_reactor: Invalid argument");
    }
    h = lua_get_flux (L, 1);
    rc = flux_reactor_run (flux_get_reactor (h), mode);
    int saved_errno = errno;
    if (rc < 0 && (reason = flux_aux_get (h, "lua::reason"))) {
        lua_pushnil (L);
        lua_pushstring (L, reason);
        return (2);
    }
    errno = saved_errno;
    return (l_pushresult (L, rc));
}

static int l_flux_reactor_stop (lua_State *L)
{
    flux_reactor_stop (flux_get_reactor (lua_get_flux (L, 1)));
    return 0;
}

static int l_flux_reactor_stop_error (lua_State *L)
{
    const char *reason;
    flux_t *h = lua_get_flux (L, 1);
    if ((lua_gettop (L) > 1) && (reason = lua_tostring (L, 2))) {
        flux_aux_set (h, "lua::reason", strdup (reason), free);
    }
    flux_reactor_stop_error (flux_get_reactor (h));
    return 0;
}

static const struct luaL_Reg flux_functions [] = {
    { "new",             l_flux_new         },
    { NULL,              NULL              }
};

static const struct luaL_Reg flux_methods [] = {
    { "__gc",            l_flux_destroy     },
    { "__index",         l_flux_index       },
    { "kvsdir",          l_flux_kvsdir_new  },
    { "kvs_symlink",     l_flux_kvs_symlink },
    { "kvs_type",        l_flux_kvs_type    },
    { "kvs_commit",      l_flux_kvs_commit  },
    { "kvs_put",         l_flux_kvs_put     },
    { "kvs_get",         l_flux_kvs_get     },
    { "kvs_unlink",      l_flux_kvs_unlink  },
    { "barrier",         l_flux_barrier     },
    { "send",            l_flux_send        },
    { "recv",            l_flux_recv        },
    { "recvmsg",         l_flux_recvmsg     },
    { "rpc",             l_flux_rpc         },
    { "sendevent",       l_flux_send_event  },
    { "recv_event",      l_flux_recv_event },
    { "subscribe",       l_flux_subscribe   },
    { "unsubscribe",     l_flux_unsubscribe },
    { "getattr",         l_flux_getattr     },
    { "msghandler",      l_msghandler_add    },
    { "statwatcher",     l_stat_watcher_add  },
    { "timer",           l_timeout_handler_add },
    { "reactor",         l_flux_reactor_start },
    { "reactor_stop",    l_flux_reactor_stop },
    { "reactor_stop_error",
                         l_flux_reactor_stop_error },
    { NULL,              NULL               }
};

static const struct luaL_Reg msghandler_methods [] = {
    { "__index",         l_msghandler_index    },
    { "__newindex",      l_msghandler_newindex },
    { "remove",          l_msghandler_remove   },
    { NULL,              NULL                  }
};

static const struct luaL_Reg watcher_methods [] = {
    { "__gc",            l_watcher_destroy  },
    { "__index",         l_watcher_index    },
    { "__newindex",      l_watcher_newindex },
    { "remove",          l_watcher_remove   },
    { NULL,              NULL                  }
};

static const struct luaL_Reg timeout_handler_methods [] = {
    { "__index",         l_timeout_handler_index    },
    { "__newindex",      l_timeout_handler_newindex },
    { "remove",          l_timeout_handler_remove   },
    { NULL,              NULL                  }
};

#define FLUX_CONSTANT_SET(L, name) do { \
  lua_pushlstring(L, #name, sizeof(#name)-1); \
  lua_pushnumber(L, FLUX_ ## name); \
  lua_settable(L, -3); \
} while (0);


int luaopen_flux (lua_State *L)
{
    luaL_newmetatable (L, "FLUX.msghandler");
    luaL_setfuncs (L, msghandler_methods, 0);
    luaL_newmetatable (L, "FLUX.watcher");
    luaL_setfuncs (L, watcher_methods, 0);
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

    FLUX_CONSTANT_SET (L, MSGTYPE_REQUEST);
    FLUX_CONSTANT_SET (L, MSGTYPE_RESPONSE);
    FLUX_CONSTANT_SET (L, MSGTYPE_EVENT);
    FLUX_CONSTANT_SET (L, MSGTYPE_ANY);
    FLUX_CONSTANT_SET (L, NODEID_ANY);
    FLUX_CONSTANT_SET (L, NODEID_UPSTREAM);

    lua_push_json_null (L);
    lua_pushliteral (L, "NULL");
    lua_settable (L, -3);

    return (1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
