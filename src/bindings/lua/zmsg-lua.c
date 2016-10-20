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
#include <lua.h>
#include <lauxlib.h>
#include <zmq.h>
#include <czmq.h>

#include "flux/core.h"
#include "lutil.h"
#include "json-lua.h"
#include "zmsg-lua.h"

/*
 *  cmb ZMSG lua binding
 */

struct zmsg_info {
    int     typemask;
    flux_msg_t *msg;
    char *tag;
    json_object *o;

    zi_resp_f resp;
    void *arg;
};

static const char * zmsg_type_string (int typemask);

struct zmsg_info * zmsg_info_create (flux_msg_t **msg, int typemask)
{
    const char *topic;
    const char *json_str;
    struct zmsg_info *zi = malloc (sizeof (*zi));
    if (zi == NULL)
        return (NULL);

    if (flux_msg_get_topic (*msg, &topic) < 0 || !(zi->tag = strdup (topic))) {
        free (zi);
        return (NULL);
    }
    zi->o = NULL;
    if (flux_msg_get_json (*msg, &json_str) < 0
                || (json_str && !(zi->o = json_tokener_parse (json_str)))) {
        free (zi->tag);
        free (zi);
        return (NULL);
    }

    zi->msg = flux_msg_copy (*msg, true);
    zi->typemask = typemask;

    zi->resp = NULL;
    zi->arg = NULL;

    return (zi);
}

static void zmsg_info_destroy (struct zmsg_info *zi)
{
    flux_msg_destroy (zi->msg);
    if (zi->o)
        json_object_put (zi->o);
    if (zi->tag)
        free (zi->tag);
    free (zi);
}

const json_object *zmsg_info_json (struct zmsg_info *zi)
{
    return (zi->o);
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

static struct zmsg_info * l_get_zmsg_info (lua_State *L, int index)
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

static const char * zmsg_type_string (int type)
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
        lua_pushstring (L, zi->tag);
        return (1);
    }
    if (strcmp (key, "data") == 0) {
        return json_object_to_lua (L, zi->o);
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
            return lua_pusherror (L, "zmsg: matchtag: %s",
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
    json_object *o;
    lua_value_to_json (L, 2, &o);
    if (o && zi->resp)
        return ((*zi->resp) (L, zi, o, zi->arg));
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

static const struct luaL_Reg zmsg_methods [] = {
    { "__gc",            l_zmsg_info_destroy  },
    { "__index",         l_zmsg_info_index    },
    { "respond",         l_zmsg_info_respond  },
    { NULL,              NULL                 }
};

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
