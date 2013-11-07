
#include <lua.h>
#include <lauxlib.h>
#include <zmq.h>
#include <czmq.h>

#include <json/json.h>

#include "util/util.h"
#include "util/zmsg.h"
#include "lutil.h"
#include "json-lua.h"
#include "zmsg-lua.h"

/*
 *  cmb ZMSG lua binding
 */

struct zmsg_info {
    zmsg_type_t type;
    zmsg_t *zmsg;
    char *tag;
    json_object *o;

    zi_resp_f resp;
    void *arg;
};

static const char * zmsg_type_string (zmsg_type_t type);

struct zmsg_info * zmsg_info_create (zmsg_t *zmsg, zmsg_type_t type)
{
    struct zmsg_info *zi = malloc (sizeof (*zi));
    if (zi == NULL)
        return (NULL);

    if (cmb_msg_decode (zmsg, &zi->tag, &zi->o) < 0) {
        free (zi);
        return (NULL);
    }

    zi->zmsg = zmsg;
    zi->type = type;

    zi->resp = NULL;
    zi->arg = NULL;

    return (zi);
}

static void zmsg_info_destroy (struct zmsg_info *zi)
{
    if (zi->zmsg)
        zmsg_destroy (&zi->zmsg);
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

const zmsg_t *zmsg_info_zmsg (struct zmsg_info *zi)
{
    return (zi->zmsg);
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

static const char * zmsg_type_string (zmsg_type_t type)
{
    switch (type) {
        case ZMSG_REQUEST:
            return ("request");
            break;
        case ZMSG_EVENT:
            return ("event");
            break;
        case ZMSG_RESPONSE:
            return ("response");
            break;
        case ZMSG_SNOOP:
            return ("snoop");
            break;
    }
    return ("Unknown");
}

static int l_zmsg_info_index (lua_State *L)
{
    struct zmsg_info *zi = l_get_zmsg_info (L, 1);
    const char *key = lua_tostring (L, 2);
    if (key == NULL)
        return luaL_error (L, "zmsg: invalid member");

    if (strcmp (key, "type") == 0) {
        lua_pushstring (L, zmsg_type_string (zi->type));
        return (1);
    }
    if (strcmp (key, "tag") == 0) {
        lua_pushstring (L, zi->tag);
        return (1);
    }
    if (strcmp (key, "data") == 0) {
        return json_object_to_lua (L, zi->o);
    }

    /* Push metatable value onto stack */
    lua_getmetatable (L, 1);
    lua_getfield (L, -1, key);
    return (1);
}

static int l_zmsg_info_respond (lua_State *L)
{
    struct zmsg_info *zi = l_get_zmsg_info (L, 1);
    json_object *o = lua_value_to_json (L, 2);
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
    luaL_register (L, NULL, zmsg_methods);
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
