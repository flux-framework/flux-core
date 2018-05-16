/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
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
#ifndef HAVE_JANSSON_LUA
#define HAVE_JANSSON_LUA
#include <lua.h>
#include <jansson.h>

void lua_push_json_null (lua_State *L);
int lua_is_json_null (lua_State *L, int index);

int json_object_to_lua (lua_State *L, json_t *o);
int json_object_string_to_lua (lua_State *L, const char *json_str);

int lua_value_to_json (lua_State *L, int index, json_t **v);
int lua_value_to_json_string (lua_State *L, int index, char **json_str);

#endif /* HAVE_JANSSON_LUA */
