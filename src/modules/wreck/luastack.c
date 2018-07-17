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
#include <lualib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>
#include <fcntl.h>
#include <glob.h>

#include "list.h"
#include "luastack.h"

/****************************************************************************
 *  Datatypes
 ****************************************************************************/

#define LUA_SCRIPT_FILE    1
#define LUA_SCRIPT_STRING  2

struct lua_script {
    int         type; /*  Type of loaded script, a file or a raw string     */
    char *      data; /*  Data: File path or string                         */
    char *      label;/*  Filename or name of this script (for errors)      */

    lua_stack_t   st; /*  Pointer back to lua_stack in which we're loaded   */
    lua_State *    L; /*  Copy of Lua state                                 */
    int      env_ref; /*  Reference for _ENV table (5.1 setfenv table)      */
};

struct lua_script_stack {
    lua_State *L;           /*  Global lua state                            */
    l_err_f    errf;
    List       script_list; /*  List of scripts in this stack               */
};


/****************************************************************************
 *  Functions
 ****************************************************************************/

static int lua_script_is_file (lua_script_t s)
{
    return (s->type == LUA_SCRIPT_FILE);
}

static int lua_script_is_string (lua_script_t s)
{
    return (s->type == LUA_SCRIPT_STRING);
}

static void lua_script_destroy (struct lua_script *s)
{
    if (s == NULL)
        return;
    if (s->L && s->st) {
        luaL_unref (s->L, LUA_REGISTRYINDEX, s->env_ref);
        /* Only call lua_close() on global/main lua state */
        s->st = NULL;
        s->L = NULL;
    }
    s->type = 0;
    free (s->data);
    free (s->label);
    free (s);
}


lua_script_t lua_script_create (lua_stack_t st, int type, const char *data)
{
    struct lua_script *script;
    struct lua_State *L;

    if (!st || !st->L)
        return (NULL);

    if (type != LUA_SCRIPT_FILE && type != LUA_SCRIPT_STRING) {
        (*st->errf) ("lua_script_create: Invalid type!");
        return (NULL);
    }

    if ((script = malloc (sizeof (*script))) == NULL)
        return (NULL);

    memset (script, 0, sizeof (*script));

    script->type = type;
    if (!(script->data = strdup (data))) {
        lua_script_destroy (script);
        return (NULL);
    }

    if (type == LUA_SCRIPT_FILE &&
        !(script->label = strdup (basename (script->data)))) {
        lua_script_destroy (script);
        return (NULL);
    }

    L = st->L;
    script->st = st;
    script->L = L;

    /*  New globals table/_ENV for this chunk */
    lua_newtable (script->L);

    /*  metatable for table on top of stack */
    lua_newtable (script->L);

    /*
     *  Now set metatable->__index to point to the real globals
     *   table. This way Lua will check the root global table
     *   for any nonexistent items in the current chunk's environment
     *   table.
     */
    lua_pushstring (script->L, "__index");
    lua_getglobal (script->L, "_G");
    lua_settable (script->L, -3);

    /*  Now set metatable for the new globals table */
    lua_setmetatable (script->L, -2);

    /* Save reference to this table, which will be used as _ENV for loaded chunk */
    script->env_ref = luaL_ref (script->L, LUA_REGISTRYINDEX);
    return (script);
}

static inline void print_lua_script_error (lua_stack_t st, lua_script_t s)
{
    (*st->errf) ("%s: %s\n", s->label, lua_tostring (s->L, -1));
}

static int lua_script_compile (lua_stack_t st, lua_script_t s)
{
    /*
     *  First load Lua script (via loadfile or loadbuffer)
     */
    if (lua_script_is_file (s) && luaL_loadfile (s->L, s->data)) {
        (*st->errf) ("%s: Script failed to load.\n", s->data);
        return (-1);
    }
    else if (lua_script_is_string (s) &&
            luaL_loadbuffer (s->L, s->data, strlen (s->data), s->label)) {
        (*st->errf) ("%s: Failed to load script.\n", s->data);
        return (-1);
    }

    /* Get the environment/globals table for this script from
     *  the registry and set it as globals table for this chunk
     */
    lua_rawgeti (s->L, LUA_REGISTRYINDEX, s->env_ref);
#if LUA_VERSION_NUM >= 502
    /* 5.2 and greater: set table as first upvalue: i.e. _ENV */
    lua_setupvalue (s->L, -2, 1);
#else
    /* 5.0, 5.1: Set table as function environment for the chunk */
    lua_setfenv (s->L, -2);
#endif

    /*
     *  Now compile the loaded script:
     */
    if (lua_pcall (s->L, 0, 0, 0)) {
        print_lua_script_error (st, s);
        return (-1);
    }

    return (0);
}

static int ef (const char *p, int eerrno)
{
    /* FIXME */
    //fprintf (stderr, "glob: %s: %s\n", p, strerror (eerrno));
    return (-1);
}

int lua_script_list_append (lua_stack_t st, const char *pattern)
{
    glob_t gl;
    size_t i;
    int rc;
    int type = LUA_SCRIPT_FILE;

    if (pattern == NULL || st == NULL || st->script_list == NULL)
        return (-1);

    rc = glob (pattern, GLOB_ERR, ef, &gl);
    switch (rc) {
        case 0:
            for (i = 0; i < gl.gl_pathc; i++) {
                struct lua_script *s;
                if (!(s = lua_script_create (st, type, gl.gl_pathv[i])) ||
                     (lua_script_compile (st, s) < 0)) {
                    (*st->errf) ("%s: Failed. Skipping.\n", gl.gl_pathv[i]);
                    lua_script_destroy (s);
                    continue;
                }
                list_append (st->script_list, s);
            }
            break;
        case GLOB_NOMATCH:
            break;
        case GLOB_NOSPACE:
            (*st->errf) ("glob: Out of memory\n");
            break;
        case GLOB_ABORTED:
            (*st->errf) ("Cannot read %s: %s\n", pattern, strerror (errno));
            break;
        default:
            (*st->errf) ("Unknown glob rc = %d\n", rc);
            break;
    }

    globfree (&gl);
    return (0);
}

static int lua_script_rc (lua_State *L)
{
    return (lua_isnumber (L, -1) ? lua_tonumber (L, -1) : 0);
}

static void verr (const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
}

void lua_stack_destroy (lua_stack_t st)
{
    if (st == NULL)
        return;
    if (st->script_list)
        list_destroy (st->script_list);
    if (st->L)
        lua_close (st->L);
    free (st);
}

lua_stack_t lua_stack_create (void)
{
    lua_stack_t s = malloc (sizeof (*s));

    if (s == NULL)
        return (NULL);

    if (!(s->script_list = list_create ((ListDelF) lua_script_destroy))) {
        free (s);
        return (NULL);
    }

    s->L = luaL_newstate ();
    s->errf = &verr;

    luaL_openlibs(s->L);

    return (s);
}

int lua_stack_append_file (lua_stack_t st, const char *pattern)
{
    if (st == NULL || st->script_list == NULL)
        return (-1);

    if (lua_script_list_append (st, pattern) < 0)
        return (-1);

    return (0);
}

int lua_stack_append_script (lua_stack_t st, const char *script,
                             const char *label)
{
    int type = LUA_SCRIPT_STRING;
    lua_script_t s = lua_script_create (st, type, script);
    if (s == NULL)
        return (-1);

    s->label = label ? strdup (label) : strdup ("<script>");

    if (lua_script_compile (st, s) < 0) {
        lua_script_destroy (s);
	return (-1);
    }

    list_append (st->script_list, s);

    return (0);
}

int lua_stack_foreach (lua_stack_t st, l_foreach_f f, void *arg)
{
    int rc = 0;
    lua_script_t s;
    ListIterator i;

    if (!st || !f)
        return (-1);

    i = list_iterator_create (st->script_list);
    while ((s = list_next (i))) {
        if ((*f) (s, arg) < 0)
            rc = -1;
    }
    list_iterator_destroy (i);

    return (rc);
}

lua_State *lua_script_state (lua_script_t s)
{
    return (s->L);
}

lua_State *lua_stack_state (lua_stack_t st)
{
    return (st->L);
}

/*
 *  Return "global" `name` from the shadow globals table for this
 *   script (stored at s->env_ref).
 */
static void lua_script_getglobal (lua_script_t s, const char *name)
{
    lua_rawgeti (s->L, LUA_REGISTRYINDEX, s->env_ref);
    lua_getfield (s->L, -1, name);
}

int lua_script_call (lua_script_t s, const char *name)
{
    struct lua_State *L = lua_script_state (s);

    lua_script_getglobal (s, name);

    if (lua_isnil (L, -1)) {
        lua_pop (L, 1);
        return (0);
    }

    if (lua_pcall (L, 0, 1, 0)) {
        (*s->st->errf) ("%s: %s: %s\n", s->label, name, lua_tostring (L, -1));
        return (-1);
    }
    return (lua_script_rc (s->L));
}

int lua_stack_call (lua_stack_t st, const char *name)
{
    return lua_stack_foreach (st, (l_foreach_f) lua_script_call, (void *)name);
}

int lua_stack_set_errfunc (lua_stack_t st, l_err_f errf)
{
    st->errf = errf;
    return (0);
}

int vec_to_lua_table (lua_State *L, char **av)
{
    char **p;
    lua_newtable (L);
    for (p = av; *p != NULL; p++) {
        char *s = *p;
        char *eq = strchr (s, '=');

        if (eq == NULL) {
            /* Should not happen */
            lua_pushstring (L, s);
            lua_pushboolean (L, 1);
        }
        else {
            lua_pushlstring (L, s, eq - s);
            lua_pushstring (L, eq+1);
        }
        lua_settable (L, -3);
    }
    return (1);
}

static void free_env (char **env, int count)
{
    int i;
    for (i = 0; i < count; i++)
        free (env[i]);
    free (env);
}

char **lua_table_to_vec (lua_State *L, int index)
{
    int count = 0;
    char **env;

    if (!lua_istable (L, index))
        return (NULL);

    /*  Push the first key: (a nil) */
    lua_pushnil (L);
    while (lua_next (L, index) != 0) {
        count++;
        lua_pop (L, 1);
    }
    /* pop final key */
    lua_pop (L, 1);

    env = malloc ((sizeof (*env) * count) + 1);
    if (env == NULL)
        return (NULL);

    count = 0;
    lua_pushnil (L);
    while (lua_next (L, index) != 0) {
        /*  'key' is at index -2 and 'value' is at index -1 */
        const char *var = lua_tostring (L, -2);
        const char *val = lua_tostring (L, -1);
        char *e = malloc (strlen (var) + strlen (val) + 2);
        if (e == NULL) {
            free_env (env, count);
            return (NULL);
        }
        sprintf (e, "%s=%s", var, val);
        env[count] = e;
        /*  pop 'value' and save key for next iteration */
        lua_pop (L, 1);
        count++;
    }
    lua_pop (L, 1);

    env[count] = NULL;

    return (env);
}


/* vi: ts=4 sw=4 expandtab
 */
