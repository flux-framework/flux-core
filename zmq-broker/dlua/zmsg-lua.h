#ifndef HAVE_ZMSG_LUA_H
#define HAVE_ZMSG_LUA_H

#include <lua.h>
#include <zmq.h>
#include <json/json.h>

struct zmsg_info;
/*
 *  Need to copy plugin.h zmsg_type_t here:
 */
typedef enum {
    ZMSG_REQUEST, ZMSG_RESPONSE, ZMSG_EVENT, ZMSG_SNOOP
} zmsg_type_t;

typedef int (*zi_resp_f) (lua_State *L,
	struct zmsg_info *zi, json_object *resp, void *arg);

struct zmsg_info *zmsg_info_create (zmsg_t *zmsg, zmsg_type_t type);

int zmsg_info_register_resp_cb (struct zmsg_info *zi, zi_resp_f f, void *arg);

const zmsg_t *zmsg_info_zmsg (struct zmsg_info *zi);

const json_object *zmsg_info_json (struct zmsg_info *zi);

int l_zmsg_info_register_metatable (lua_State *L);

int lua_push_zmsg_info (lua_State *L, struct zmsg_info *zi);

#endif
