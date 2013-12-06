
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>

#include <json/json.h>

#include "cmb.h"
#include "flux.h"
#include "mrpc.h"

#include "json-lua.h"
#include "kvs-lua.h"
#include "zmsg-lua.h"
#include "lutil.h"

int lua_push_flux_handle (lua_State *L, flux_t f)
{
    flux_t *fp = lua_newuserdata (L, sizeof (*fp));
    *fp = f;
    luaL_getmetatable (L, "FLUX.handle");
    lua_setmetatable (L, -2);
    return (1);
}

static flux_t lua_get_flux (lua_State *L, int index)
{
    flux_t *fluxp = luaL_checkudata (L, index, "FLUX.handle");
    return (*fluxp);
}

static int l_flux_new (lua_State *L)
{
    flux_t *fluxp = lua_newuserdata (L, sizeof (*fluxp));
    *fluxp = cmb_init ();

    if (*fluxp == NULL)
        return lua_pusherror (L, strerror (errno));

    luaL_getmetatable (L, "FLUX.handle");
    lua_setmetatable (L, -2);
    return (1);
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

static int l_flux_destroy (lua_State *L)
{
    flux_t f = lua_get_flux (L, 1);
    lua_pop (L, 1);
    flux_handle_destroy (&f);
    return (0);
}

static int l_flux_barrier (lua_State *L)
{
    flux_t f = lua_get_flux (L, 1);
    const char *name = luaL_checkstring (L, 2);
    int nprocs = luaL_checkinteger (L, 3);
    return (l_pushresult (L, flux_barrier (f, name, nprocs)));
}

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

    json_object *o = lua_value_to_json (L, 3);

    if (tag == NULL)
        lua_pusherror (L, "Invalid args");

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
    json_object *o = lua_value_to_json (L, 3);
    json_object *resp;

    if (tag == NULL || o == NULL)
        lua_pusherror (L, "Invalid args");

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
        o = lua_value_to_json (L, 2);
    }

    if ((l_format_args (L, eventidx) < 0))
        return (2); /* nil, err */

    event = luaL_checkstring (L, -1);

    rc = flux_event_send (f, o, event);
    if (o)
        json_object_put (o);

    return l_pushresult (L, rc);
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
        json_object *o = lua_value_to_json (L, 3);
        if (o == NULL)
            return lua_pusherror (L, "Failed to create json from argument");
        flux_mrpc_put_inarg (m, o);
        json_object_put (o);
        return (0);
    }
    if (strcmp (key, "out") == 0) {
        json_object *o = lua_value_to_json (L, 3);
        if (o == NULL)
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
        json_object *o = lua_value_to_json (L, 3);
        flux_mrpc_put_inarg (m, o);
        json_object_put (o);
    }

    return lua_push_mrpc (L, m);
}

static const struct luaL_Reg flux_functions [] = {
    { "new",             l_flux_new         },
    { NULL,              NULL              }
};

static const struct luaL_Reg flux_methods [] = {
    { "__gc",            l_flux_destroy     },
    { "__index",         l_flux_index       },
    { "kvsdir",          l_flux_kvsdir_new  },
    { "barrier",         l_flux_barrier     },
    { "send",            l_flux_send        },
    { "recv",            l_flux_recv        },
    { "rpc",             l_flux_rpc         },
    { "mrpc",            l_flux_mrpc_new    },
    { "sendevent",       l_flux_send_event  },
    { "subscribe",       l_flux_subscribe   },
    { "unsubscribe",     l_flux_unsubscribe },
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

int luaopen_flux (lua_State *L)
{
    luaL_newmetatable (L, "FLUX.mrpc");
    luaL_register (L, NULL, mrpc_methods);
    luaL_newmetatable (L, "FLUX.mrpc_outarg");
    luaL_register (L, NULL, mrpc_outargs_methods);

    luaL_newmetatable (L, "FLUX.handle");
    luaL_register (L, NULL, flux_methods);
    /*
     * Load required kvs library
     */
    luaopen_kvs (L);
    luaL_register (L, "flux", flux_functions);
    return (1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
