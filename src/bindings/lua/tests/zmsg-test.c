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
#include <errno.h>
#include <string.h>

#include "flux/core.h"
#include "json-lua.h"
#include "zmsg-lua.h"
#include "lutil.h"


zmsg_t *l_cmb_zmsg_encode (lua_State *L)
{
	const char *tag = lua_tostring (L, 1);
	json_object *o;

    lua_value_to_json (L, 2, &o);
	if ((o == NULL) || (tag == NULL))
		return NULL;

    return (flux_msg_encode ((char *)tag, o));
}

static int l_zi_resp_cb (lua_State *L,
    struct zmsg_info *zi, json_object *resp, void *arg)
{
    zmsg_t **old = zmsg_info_zmsg (zi);
    zmsg_t **zmsg = malloc (sizeof (*zmsg));
    *zmsg = zmsg_dup (*old);

    if (flux_msg_replace_json (*zmsg, resp) < 0)
        return lua_pusherror (L, "cmb_msg_replace_json: %s", strerror (errno));

    return lua_push_zmsg_info (L, zmsg_info_create (zmsg, FLUX_MSGTYPE_RESPONSE));
}

static int l_cmb_zmsg_create_type (lua_State *L, int type)
{
    struct zmsg_info *zi;
	zmsg_t **zmsg = malloc (sizeof (*zmsg));
	if ((*zmsg = l_cmb_zmsg_encode (L)) == NULL)
        return luaL_error (L, "Failed to encode zmsg");
    zi = zmsg_info_create (zmsg, type);
    zmsg_info_register_resp_cb (zi, l_zi_resp_cb, NULL);

	return lua_push_zmsg_info (L, zi);
}

static int l_cmb_zmsg_create_response (lua_State *L)
{
    return l_cmb_zmsg_create_type (L, FLUX_MSGTYPE_RESPONSE);
}

static int l_cmb_zmsg_create_request (lua_State *L)
{
    return l_cmb_zmsg_create_type (L, FLUX_MSGTYPE_REQUEST);
}

static int l_cmb_zmsg_create_event (lua_State *L)
{
    return l_cmb_zmsg_create_type (L, FLUX_MSGTYPE_EVENT);
}

static const struct luaL_Reg zmsg_info_test_functions [] = {
	{ "req",       l_cmb_zmsg_create_request   },
	{ "resp",      l_cmb_zmsg_create_response  },
	{ "event",     l_cmb_zmsg_create_event     },
	{ NULL,        NULL              }
};

int luaopen_zmsgtest (lua_State *L)
{
    l_zmsg_info_register_metatable (L);
	luaL_register (L, "zmsgtest", zmsg_info_test_functions);
	return (1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
