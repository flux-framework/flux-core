/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_ZMSG_LUA_H
#define HAVE_ZMSG_LUA_H

#include <lua.h>
#include <zmq.h>
#include <jansson.h>

#include "flux/core.h"

struct zmsg_info;

typedef int (*zi_resp_f) (lua_State *L,
	struct zmsg_info *zi, const char *json_str, void *arg);

struct zmsg_info *zmsg_info_create (flux_msg_t **msg, int type);

int zmsg_info_register_resp_cb (struct zmsg_info *zi, zi_resp_f f, void *arg);

flux_msg_t **zmsg_info_zmsg (struct zmsg_info *zi);

int l_zmsg_info_register_metatable (lua_State *L);

int lua_push_zmsg_info (lua_State *L, struct zmsg_info *zi);

#endif
