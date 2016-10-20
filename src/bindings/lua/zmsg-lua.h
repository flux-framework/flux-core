#ifndef HAVE_ZMSG_LUA_H
#define HAVE_ZMSG_LUA_H

#include <lua.h>
#include <zmq.h>
#include "src/common/libjson-c/json.h"

#include "flux/core.h"

struct zmsg_info;

typedef int (*zi_resp_f) (lua_State *L,
	struct zmsg_info *zi, json_object *resp, void *arg);

struct zmsg_info *zmsg_info_create (flux_msg_t **msg, int type);

int zmsg_info_register_resp_cb (struct zmsg_info *zi, zi_resp_f f, void *arg);

flux_msg_t **zmsg_info_zmsg (struct zmsg_info *zi);

const json_object *zmsg_info_json (struct zmsg_info *zi);

int l_zmsg_info_register_metatable (lua_State *L);

int lua_push_zmsg_info (lua_State *L, struct zmsg_info *zi);

#endif
