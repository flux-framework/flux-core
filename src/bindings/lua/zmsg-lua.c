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
#include <lua.h>
#include <lauxlib.h>
#include <zmq.h>
#include <czmq.h>

#include "flux/core.h"
#include "lutil.h"
#include "jansson-lua.h"
#include "zmsg-lua.h"

/*
 *  cmb ZMSG lua binding
 */

struct zmsg_info {
    int typemask;    /* Type of message */
    flux_msg_t *msg; /* Stored copy of original message */
    char *tag;       /* Topic tag for message */
    json_t *o;       /* Decode JSON payload, NULL if no payload */

    zi_resp_f resp; /* Respond handler (for msg:respond() method) */
    void *arg;      /* data passed to respond handler */
};

static const char *zmsg_type_string (int typemask);

static void zmsg_info_destroy (struct zmsg_info *zi)
{
    flux_msg_destroy (zi->msg);
    json_decref (zi->o);
    free (zi->tag);
    free (zi);
}

struct zmsg_info *zmsg_info_create (flux_msg_t **msg, int typemask)
{
    const char *topic;
    const char *json_str = NULL;
    struct zmsg_info *zi = calloc (1, sizeof (*zi));
    if (zi == NULL)
        return (NULL);

    if ((flux_msg_get_topic (*msg, &topic) < 0) || !(zi->tag = strdup (topic))
        || !(zi->msg = flux_msg_copy (*msg, true))
        || (flux_msg_get_string (zi->msg, &json_str) < 0)) {
        zmsg_info_destroy (zi);
        return (NULL);
    }
    /* If there was a payload, cache decoded JSON into zi->o */
    if (json_str && !(zi->o = json_loads (json_str, JSON_DECODE_ANY, NULL))) {
        zmsg_info_destroy (zi);
        return (NULL);
    }
    zi->typemask = typemask;
    zi->resp = NULL;
    zi->arg = NULL;

    return (zi);
}

flux_msg_t **zmsg_info_zmsg (struct zmsg_info *zi)
{
    return (&zi->msg);
}

int zmsg_info_register_resp_cb (struct zmsg_info *zi, zi_resp_f f, void *arg)
{
    zi->resp = f;
    zi->arg = arg;
    return (0);
}

static struct zmsg_info *l_get_zmsg_info (lua_State *L, int index)
{
    struct zmsg_info **zip = luaL_checkudata (L, index, "CMB.zmsgi");
    return (*zip);
}

static int l_zmsg_info_destroy (lua_State *L)
{
    struct zmsg_info *zi = l_get_zmsg_info (L, 1);
    zmsg_info_destroy (zi);
    return (0);
}

static const char *zmsg_type_string (int type)
{
    switch (type) {
    case FLUX_MSGTYPE_REQUEST:
        return ("request");
    case FLUX_MSGTYPE_EVENT:
        return ("event");
    case FLUX_MSGTYPE_RESPONSE:
        return ("response");
    case FLUX_MSGTYPE_ANY:
        return ("all");
    default:
        break;
    }
    return ("Unknown");
}

static int l_zmsg_info_index (lua_State *L)
{
    struct zmsg_info *zi = l_get_zmsg_info (L, 1);
    const char *key = lua_tostring (L, 2);
    if (key == NULL)
        return lua_pusherror (L, "zmsg: invalid member");

    if (strcmp (key, "type") == 0) {
        lua_pushstring (L, zmsg_type_string (zi->typemask));
        return (1);
    }
    if (strcmp (key, "tag") == 0) {
        if (zi->tag)
            lua_pushstring (L, zi->tag);
        else
            lua_pushnil (L);
        return (1);
    }
    if (strcmp (key, "data") == 0) {
        if (!zi->o || json_object_to_lua (L, zi->o) < 0)
            lua_pushnil (L);
        return (1);
    }
    if (strcmp (key, "errnum") == 0) {
        int errnum;
        if (!(zi->typemask & FLUX_MSGTYPE_RESPONSE))
            return lua_pusherror (L,
                                  "zmsg: errnum requested for non-respose msg");
        flux_msg_get_errnum (zi->msg, &errnum);
        lua_pushnumber (L, errnum);
        return (1);
    }
    if (strcmp (key, "matchtag") == 0) {
        uint32_t matchtag;
        if (flux_msg_get_matchtag (zi->msg, &matchtag) < 0)
            return lua_pusherror (L,
                                  "zmsg: matchtag: %s",
                                  (char *)flux_strerror (errno));
        lua_pushnumber (L, matchtag);
        return (1);
    }

    /* Push metatable value onto stack */
    lua_getmetatable (L, 1);
    lua_getfield (L, -1, key);
    return (1);
}

static int l_zmsg_info_respond (lua_State *L)
{
    struct zmsg_info *zi = l_get_zmsg_info (L, 1);
    char *json_str = NULL;
    int rc = -1;
    if (lua_value_to_json_string (L, 2, &json_str) < 0)
        return lua_pusherror (L, "JSON conversion error");
    if (json_str && zi->resp)
        rc = ((*zi->resp) (L, zi, json_str, zi->arg));
    free (json_str);
    if (rc >= 0)
        return rc;
    return lua_pusherror (L, "zmsg_info_respond: Not implemented");
}

int lua_push_zmsg_info (lua_State *L, struct zmsg_info *zi)
{
    struct zmsg_info **zip = lua_newuserdata (L, sizeof (*zip));
    *zip = zi;
    luaL_getmetatable (L, "CMB.zmsgi");
    lua_setmetatable (L, -2);
    return (1);
}

static const struct luaL_Reg zmsg_methods[] = {{"__gc", l_zmsg_info_destroy},
                                               {"__index", l_zmsg_info_index},
                                               {"respond", l_zmsg_info_respond},
                                               {NULL, NULL}};

int l_zmsg_info_register_metatable (lua_State *L)
{
    luaL_newmetatable (L, "CMB.zmsgi");
    luaL_setfuncs (L, zmsg_methods, 0);
    return (1);
}

int luaopen_zmsg (lua_State *L)
{
    l_zmsg_info_register_metatable (L);
    return (1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
