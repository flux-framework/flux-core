/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/****************************************************************************
 *
 *  luastack.h - Support for loading and maintaining a stack of lua scripts
 *   either from filenames/patterns, or raw strings.
 *
 ****************************************************************************/

#ifndef HAVE_LUASTACK_H
#define HAVE_LUASTACK_H
#include <lua.h>

typedef struct lua_script_stack * lua_stack_t;
typedef struct lua_script *       lua_script_t;

typedef int  (*l_foreach_f) (lua_script_t s, void *arg);
typedef void (*l_err_f)     (const char *fmt, ...);

/*
 *   Create a lua_stack object for loading stacks of lua scripts
 *    with common callback functions. A global Lua state is created
 *    at the time of creation and standard libraries are opened.
 */
lua_stack_t lua_stack_create (void);

/*
 *   Destroy lua stack [c] closing the global Lua state and freeing
 *    associated memory.
 */
void lua_stack_destroy (lua_stack_t s);

/*
 *   Set error output function for lua stack [s] to function [f]
 */
int lua_stack_set_errfunc (lua_stack_t s, l_err_f f);

/*
 *  Load a set of Lua plugins that match the glob(7) pattern [pattern]
 *   into the Lua stack [s]. For example pattern = "/etc/config/ *.lua"
 *   or to load a single script "/etc/config/test.lua".
 *
 *  This results in all scripts matching [pattern] to be loaded and
 *   compiled, but no functions within the defined scripts are called.
 *
 *  Returns 0 for Success, -1 for failure.
 *
 */
int lua_stack_append_file (lua_stack_t s, const char *pattern);

/*
 *  Load a script in buffer [s] into the Lua stack [st] and give it a
 *   label of [name]
 *
 *  Returns 0 for success, -1 for failure.
 *
 */
int lua_stack_append_script (lua_stack_t st, const char *s, const char *name);

/*
 *  Return the global lua_State from lua stack s
 */
lua_State *lua_stack_state (lua_stack_t s);

/*
 *  Return local lua state for script [s]
 */
lua_State *lua_script_state (lua_script_t s);

/*
 *  Call function [name] in lua script [s]
 */
int lua_script_call (lua_script_t s, const char *name);

/*
 *  Call function [name] in all scripts with no arguments.
 */
int lua_stack_call (lua_stack_t s, const char *name);

/*
 *  Execute callback function [cb] for each Lua script in stack [s].
 *   Argument [arg] will be passed to each invocation of the callback.
 */
int lua_stack_foreach (lua_stack_t s, l_foreach_f cb, void *arg);


#endif /* HAVE_LUASTACK_H */
