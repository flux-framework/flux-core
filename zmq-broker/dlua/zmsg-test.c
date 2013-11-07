
#include <lua.h>
#include <lauxlib.h>
#include <zmq.h>
#include <czmq.h>
#include <errno.h>
#include <string.h>

#include <json/json.h>

#include "util/util.h"
#include "util/zmsg.h"
#include "json-lua.h"
#include "zmsg-lua.h"
#include "lutil.h"


zmsg_t *l_cmb_zmsg_encode (lua_State *L)
{
	const char *tag = lua_tostring (L, 1);
	json_object *o = lua_value_to_json (L, 2);

	if ((o == NULL) || (tag == NULL))
		return NULL;

    return (cmb_msg_encode ((char *)tag, o));
}

static int l_zi_resp_cb (lua_State *L,
    struct zmsg_info *zi, json_object *resp, void *arg)
{
    zmsg_t *zmsg = zmsg_dup ((zmsg_t *) zmsg_info_zmsg (zi));

    if (cmb_msg_replace_json (zmsg, resp) < 0)
        return lua_pusherror (L, "cmb_msg_replace_json: %s", strerror (errno));

    return lua_push_zmsg_info (L, zmsg_info_create (zmsg, ZMSG_RESPONSE));
}

static int l_cmb_zmsg_create_type (lua_State *L, zmsg_type_t type)
{
    struct zmsg_info *zi;
	zmsg_t *zmsg;
	if ((zmsg = l_cmb_zmsg_encode (L)) == NULL)
        return luaL_error (L, "Failed to encode zmsg");
    zi = zmsg_info_create (zmsg, type);
    zmsg_info_register_resp_cb (zi, l_zi_resp_cb, NULL);

	return lua_push_zmsg_info (L, zi);
}

static int l_cmb_zmsg_create_response (lua_State *L)
{
    return l_cmb_zmsg_create_type (L, ZMSG_RESPONSE);
}

static int l_cmb_zmsg_create_request (lua_State *L)
{
    return l_cmb_zmsg_create_type (L, ZMSG_REQUEST);
}

static int l_cmb_zmsg_create_event (lua_State *L)
{
    return l_cmb_zmsg_create_type (L, ZMSG_EVENT);
}

static int l_cmb_zmsg_create_snoop (lua_State *L)
{
    return l_cmb_zmsg_create_type (L, ZMSG_SNOOP);
}

static const struct luaL_Reg zmsg_info_test_functions [] = {
	{ "req",       l_cmb_zmsg_create_request   },
	{ "resp",      l_cmb_zmsg_create_response  },
	{ "event",     l_cmb_zmsg_create_event     },
	{ "snoop",     l_cmb_zmsg_create_snoop     },
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
