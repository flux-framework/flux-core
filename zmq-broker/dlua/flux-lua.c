
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
        return luaL_error (L, "flux_init: %s", strerror (errno));

    luaL_getmetatable (L, "FLUX.handle");
    lua_setmetatable (L, -2);
    return (1);
}

static int l_flux_kvsdir_new (lua_State *L)
{
    const char *path = ".";
    kvsdir_t *dirp;
    flux_t f = lua_get_flux (L, 1);

    if (lua_isstring (L, 2)) {
        /*
         *  Format string if path given as > 1 arg:
         */
        if ((lua_gettop (L) > 2) && (l_format_args (L, 2) < 0))
            return (2);
        path = lua_tostring (L, 2);
    }

    dirp = lua_newuserdata (L, sizeof (*dirp));
    if (kvs_get_dir (f, dirp, path) < 0)
        return luaL_error (L, "kvs_get_dir: %s", strerror (errno));
    return l_kvsdir_instantiate (L);
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
    { "sendevent",       l_flux_send_event  },
    { "subscribe",       l_flux_subscribe   },
    { "unsubscribe",     l_flux_unsubscribe },
    { NULL,              NULL               }
};

int luaopen_flux (lua_State *L)
{
    luaL_newmetatable (L, "FLUX.handle");
    luaL_register (L, NULL, flux_methods);
    /*
     * Load required kvs library
     */
    l_loadlibrary (L, "kvs");
    luaL_register (L, "flux", flux_functions);
    return (1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
