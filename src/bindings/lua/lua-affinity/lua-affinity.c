/*****************************************************************************
 *
 *  Copyright (C) 2007-2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory.
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *
 *  UCRL-CODE-235358
 *
 *  This file is part of slurm-spank-plugins, a set of spank plugins for SLURM.
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

#define _GNU_SOURCE
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "cpuset-str.h"

#define MAX_LUAINT ((1ULL<<(8*(sizeof(lua_Integer)-1)))-0x10)

static cpu_set_t * l_cpu_set_alloc (lua_State *L)
{
    cpu_set_t *setp = lua_newuserdata (L, sizeof (*setp));
    luaL_getmetatable (L, "CpuSet");
    lua_setmetatable (L, -2);
    return (setp);
}

static int lua_string_to_cpu_setp (lua_State *L, int index, cpu_set_t *setp)
{
    int err = 0;
    const char *s = luaL_checkstring (L, index);

    /*
     *  If string begins with 0x, 00, or contains long string of digits
     *   separated by ',', or cstr_to_cpuset() fails then try hex_to_cpuset().
     */
    if ((memcmp (s, "0x", 2L) == 0)
            || (memcmp (s, "00", 2L) == 0)
            || ((strchr (s, ',') - s) >= 8)
            || ((err = cstr_to_cpuset (setp, s)) && err != E2BIG)) {
        err = hex_to_cpuset (setp, s);
    }

    if (err) {
        /*
         *  Push (nil, error_msg) onto stack for return
         */
        lua_pushnil (L);
        lua_pushfstring (L, "unable to parse CPU mask or list: '%s'", s);
        return (2);
    }
    return (1);
}

static int lua_number_to_cpu_setp (lua_State *L, int index, cpu_set_t *setp)
{
    char buf [1024];
    unsigned long long n = lua_tointeger (L, index);

    if (n >= (unsigned long long) MAX_LUAINT || lua_tonumber (L, index) != n) {
        lua_pushnil (L);
        lua_pushfstring (L, "unable to parse CPU mask: numeric overflow");
        return (2);
    }

    /*
     *   Number is always treated like a bitmask
     */
    snprintf (buf, sizeof (buf), "0x%Lx", (unsigned long long) n);
    lua_pushstring (L, buf);

    /*
     *   replace number at index with its string equivalent:
     */
    lua_replace (L, index);

    return (lua_string_to_cpu_setp (L, index, setp));
}

/*
 *  Return cpu_set_t object at  index in the lua stack, or convert
 *   a number or string to a cpu_set mask
 */
static cpu_set_t *lua_to_cpu_setp (lua_State *L, int index)
{
    cpu_set_t *setp;

    if (lua_isuserdata (L, index))
        return luaL_checkudata (L, index, "CpuSet");

    setp = l_cpu_set_alloc (L);
    if (lua_isnil (L, index))
        CPU_ZERO (setp);
    else if (lua_type (L, index) == LUA_TNUMBER) {
        if (lua_number_to_cpu_setp (L, index, setp) == 2)
            return (NULL);
    }
    else if (lua_string_to_cpu_setp (L, index, setp) == 2)
        return (NULL);

    /*  Replace item at index with this cpu_set_t */
    lua_replace (L, index);

    return setp;
}

static int l_cpu_set_new (lua_State *L)
{
    /*
     *   If no arguments passed on stack (stack is empty)
     *    push zeroed cpu_set_t onto stack, otherwise, convert
     *    argument at position 1 to cpu_set_t and return that.
     */
    if (lua_gettop (L) == 0) {
        cpu_set_t *setp = l_cpu_set_alloc (L);
        CPU_ZERO (setp);
    }
    else if ((lua_gettop (L) == 1)) {
        if (!lua_to_cpu_setp (L, 1))
            return (2); /* Error returns (nil, msg) */
    }
    else if (lua_istable (L, 1))
        luaL_error (L, "Table is 1st arg to new(), did you mean cpu_set.new()"); 
    else
        luaL_error (L, "Expected < 2 arguments to new, got %d", lua_gettop (L));

    return (1);
}

static int l_cpu_set_count (lua_State *L)
{
    int i, n;
    cpu_set_t *setp = lua_to_cpu_setp (L, 1);

    n = 0;
    for (i = 0; i < CPU_SETSIZE; i++)
        if (CPU_ISSET (i, setp)) n++;

    lua_pushnumber (L, n);
    return (1);
}

static void cpu_set_union (cpu_set_t *setp, cpu_set_t *s)
{
    int i;
    for (i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET (i, s))
            CPU_SET (i, setp);
    }
}

static int l_cpu_set_union (lua_State *L)
{
    cpu_set_t *setp;
    int i;
    int nargs = lua_gettop (L);

    /* Accumulate results in first cpu_set
     */
    if (!(setp = lua_to_cpu_setp (L, 1)))
        return (2);

    for (i = 2; i < nargs+1; i++) {
        cpu_set_t *arg = lua_to_cpu_setp (L, i);
        if (arg == NULL)
            return (2);
        cpu_set_union (setp, arg);
    }
    /*
     *  Return first cpu_set object (set stack top to position 1):
     */
    lua_settop (L, 1);
    return (1);
}

static void cpu_set_intersect (cpu_set_t *setp, cpu_set_t *s)
{
    int i;
    /*
     *  Clear all bits set in setp that are not set in s
     */
    for (i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET (i, setp) && !CPU_ISSET (i, s)) {
            CPU_CLR (i, setp);
        }
    }
}

static int l_cpu_set_intersect (lua_State *L)
{
    int i;
    cpu_set_t *setp;
    int nargs = lua_gettop (L);

    /*
     * Accumulate results in first cpu_set
     */
    setp = lua_to_cpu_setp (L, 1);
    if (setp == NULL)
        return (2);

    for (i = 2; i < nargs+1; i++) {
        cpu_set_t *arg = lua_to_cpu_setp (L, i);
        if (arg == NULL)
            return (2);
        cpu_set_intersect (setp, arg);
    }
    /*
     * Return the first cpu_set object:
     */
    lua_settop (L, 1);
    return (1);
}

static int l_cpu_set_add (lua_State *L)
{
    cpu_set_t *result = l_cpu_set_alloc (L);
    cpu_set_t *s1;
    cpu_set_t *s2;
    int i;

    if (   !(s1 = lua_to_cpu_setp (L, 1))
        || !(s2 = lua_to_cpu_setp (L, 2)))
        return (2); /* XXX: can this happen? */

    for (i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET (i, s1) || CPU_ISSET (i, s2))
            CPU_SET (i, result);
        else
            CPU_CLR (i, result);
    }

    return (1);
}

static int l_cpu_set_set (lua_State *L)
{
    int i;
    cpu_set_t *setp = lua_to_cpu_setp (L, 1);
    int nargs = lua_gettop (L);

    for (i = 2; i < nargs+1; i++) {
        int cpu = luaL_checknumber (L, i);
        CPU_SET (cpu, setp);
    }

    /*  Doesn't return anything */
    return (0);
}

static int l_cpu_set_delete (lua_State *L)
{
    int i;
    cpu_set_t *setp = lua_to_cpu_setp (L, 1);
    int nargs = lua_gettop (L);

    for (i = 2; i < nargs+1; i++) {
        int cpu = lua_tonumber (L, i);
        CPU_CLR (cpu, setp);
    }

    return (0);
}

/*
 *  Subtract CPUs in s2 from set s1, return result in a
 *   newly allocated cpu_set.
 */
static int l_cpu_set_subtract (lua_State *L)
{
    cpu_set_t *result = l_cpu_set_alloc (L);
    cpu_set_t *s1 = lua_to_cpu_setp (L, 1);
    cpu_set_t *s2 = lua_to_cpu_setp (L, 2);
    int i;

    if (s2 == NULL)
        return (2);

    for (i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET (i, s1) && !CPU_ISSET (i, s2))
            CPU_SET (i, result);
        else
            CPU_CLR (i, result);
    }

    return (1);
}


static int l_cpu_set_zero (lua_State *L)
{
    cpu_set_t *setp = lua_to_cpu_setp (L, 1);
    CPU_ZERO (setp);
    return (0);
}

static int cpu_set_to_string (lua_State *L, int index)
{
    char buf [8192];
    cpu_set_t *setp = lua_to_cpu_setp (L, index);

    cpuset_to_cstr (setp, buf);
    lua_pushstring (L, buf);
    lua_replace (L, index);
    return (1);
}

static int l_cpu_set_tostring (lua_State *L)
{
    cpu_set_to_string (L, 1);
    lua_settop (L, 1);
    return (1);
}

static int l_cpu_set_strconcat (lua_State *L)
{
    /*
     *  Lua stack should have two args to strconcat.
     *   Convert one or both to a string as needed.
     */
    if (lua_isuserdata (L, 1))
        cpu_set_to_string (L, 1);

    if (lua_isuserdata (L, 2))
        cpu_set_to_string (L, 2);

    lua_concat (L, 2);
    return (1);
}

/*
 *  Lua wrapper for CPU_ISSET() macro.
 */
static int l_cpu_set_isset (lua_State *L)
{
    cpu_set_t *setp = lua_to_cpu_setp (L, 1);
    int i = luaL_checknumber (L, 2);
    int isset;

    if ((i < 0) || i > (CPU_SETSIZE - 1))
        return luaL_error (L, "Invalid index %d to cpu_set", i);

    isset = CPU_ISSET (i, setp);
    lua_settop (L, 0);
    lua_pushboolean (L, isset);
    return (1);
}

/*
 *  Check if set1 is contained within set2
 */
static int l_cpu_set_is_in (lua_State *L)
{
    cpu_set_t *s1 = lua_to_cpu_setp (L, 1);
    cpu_set_t *s2 = lua_to_cpu_setp (L, 2);
    int rv = 1;
    int i;

    /*
     *  s1 is in cpu_set s2 if all bits that are set in s1
     *   are also set in s2:
     */
    for (i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET (i, s1) && !CPU_ISSET (i, s2)) {
            rv = 0;
            break;
        }
    }

    lua_pop (L, 2);
    lua_pushboolean (L, rv);
    return (1);
}

static int l_cpu_set_contains (lua_State *L)
{
    /*  Same as is_in, but different order of args
     *   Swap args and call is_in():
     */
    lua_pushvalue (L, 1);
    lua_remove (L, 1);
    return (l_cpu_set_is_in (L));
}

static int l_cpu_set_equals (lua_State *L)
{
    cpu_set_t *s1 = lua_to_cpu_setp (L, 1);
    cpu_set_t *s2 = lua_to_cpu_setp (L, 2);
    int rv = 1;
    int i;

    /*
     *  Check that all CPU_SETSIZE bits are equivalent:
     */
    for (i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET (i, s1) != CPU_ISSET (i, s2)) {
            rv = 0;
            break;
        }
    }

    lua_pop (L, 2);
    lua_pushboolean (L, rv);
    return (1);
}

static int l_cpu_set_index (lua_State *L)
{
    const char *key = lua_tostring (L, 2);

    if (key == NULL)
        return luaL_error (L, "cpu_set: invalid index");

    if (strcmp (key, "size") == 0) {
        lua_pushnumber (L, CPU_SETSIZE);
        return (1);
    }

    /*  Translate function index to actual function
     */
    lua_getmetatable (L, 1);
    lua_getfield (L, -1, key);
    if (!lua_isnil (L, -1)) {
        return (1);
    }
    else if (!lua_tonumber (L, 2) && strcmp (key, "0") != 0)
        return (1);
    /*
     *  Remove nil
     */
    lua_pop (L, 1);

    /*
     *  Else cpu_set[n] was requested, so return the value of
     *   the nth bit
     */
    return (l_cpu_set_isset (L));
}

static int l_cpu_set_newindex (lua_State *L)
{
    cpu_set_t *setp = lua_to_cpu_setp (L, 1);
    int index       = luaL_checknumber (L, 2);
    int value;

    /*
     *  value is allowed to be boolean or 0 or 1.
     */
    if (lua_isboolean (L, 3))
        value = lua_toboolean (L, 3);
    else
        value = luaL_checknumber (L, 3);

    if ((index < 0) || (index >= CPU_SETSIZE))
        return luaL_error (L, "Invalid index %d to cpu_set", index);

    if (value == 1)
        CPU_SET (index, setp);
    else if (value == 0)
        CPU_CLR (index, setp);
    else
        return luaL_error (L, "Index of cpu_set may only be set to 0 or 1");

    lua_pop (L, 3);
    return (0);
}

/*
 *  cpu_set iterator (C closure)
 */
static int l_cpu_set_iterator (lua_State *L)
{
    cpu_set_t *setp;
    int bit;

    setp = lua_to_cpu_setp (L, lua_upvalueindex (1));
    bit = lua_tonumber (L, lua_upvalueindex (2));

    for (; bit < CPU_SETSIZE; bit++)
        if (CPU_ISSET (bit, setp))
            break;


    /*  Push this bit number */
    lua_pushnumber (L, bit);

    /*  Push next bit number to update closure */
    lua_pushnumber (L, bit+1);
    lua_replace (L, lua_upvalueindex (2));

    if (bit >= CPU_SETSIZE)
        return (0);

    return (1);
}

static int l_cpu_set_iterate (lua_State *L)
{
    lua_pushnumber (L, 0); /* Starting at bit 0 */
    /*
     *  Iterator creation function returns:
     *   (iterator, state, starting val))
     *  We return (iterator, nil, nil) to start at the begining
     */
    lua_pushcclosure (L, l_cpu_set_iterator, 2);
    return (1);
}

static int l_cpu_set_first (lua_State *L)
{
    cpu_set_t *setp = lua_to_cpu_setp (L, 1);
    int i;

    for (i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET (i, setp)) {
            lua_pushnumber (L, i);
            return (1);
        }
    }

    return (0);
}

static int l_cpu_set_last (lua_State *L)
{
    cpu_set_t *setp = lua_to_cpu_setp (L, 1);
    int i;

    for (i = CPU_SETSIZE - 1; i >= 0; --i) {
        if (CPU_ISSET (i, setp)) {
            lua_pushnumber (L, i);
            return (1);
        }
    }
    return (0);
}

static int l_cpu_set_tohex (lua_State *L)
{
    char buf [1024] = "0x";
    cpu_set_t *setp = lua_to_cpu_setp (L, 1);

    cpuset_to_hex (setp, buf+2, sizeof (buf)-2, "");

    lua_pushstring (L, buf);

    return (1);
}

static int l_cpu_set_expand (lua_State *L)
{
    cpu_set_t *setp;
    int has_function;
    int i, n, t;
    /*
     *  Expand CPUs set in setp int o a lua table, optionally
     *   applying a function to each element:
     */
    setp = lua_to_cpu_setp (L, 1);
    has_function = lua_isfunction (L, 2);

    /*
     *  New table at top of stack to hold results:
     */
    lua_newtable (L);
    t = lua_gettop (L);

    n = 1;
    for (i = 0; i < CPU_SETSIZE; i++) {
        /*  Skip CPUs not in this cpu_set */
        if (!CPU_ISSET (i, setp))
            continue;
        /*
         *  If there is a function to run, copy it onto the top of the
         *   stack so it may be consumed by lua_pcall()
         */
        if (has_function)
            lua_pushvalue (L, 2);

        /*
         *  Push CPU id, either to be used as argument to function, or
         *   as an element for the table:
         */
        lua_pushnumber (L, i);

        /*
         *  Call function if needed and leave the result on the stack.
         *   if the function has a compilation error, raise the error:
         */
        if (has_function && lua_pcall (L, 1, 1, 0) != 0)
            return luaL_error (L, "cpu_set.expand: %s", lua_tostring (L, -1));

        /*
         *   Only push entries that are not nil or false onto table
         */
        if (lua_isnil (L, -1))
            lua_pop (L, 1);
        else if (lua_isboolean (L, -1) && (lua_toboolean (L, -1) == 0))
            lua_pop (L, 1);
        else
            lua_rawseti (L, t, n++);
    }

    return (1);
}

static int l_cpu_set_copy (lua_State *L)
{
    cpu_set_t *setp = lua_to_cpu_setp (L, 1);
    cpu_set_t *copy = l_cpu_set_alloc (L);
    *copy = *setp;
    return (1);
}

int l_getaffinity (lua_State *L)
{
    cpu_set_t *setp = l_cpu_set_alloc (L);

    if (sched_getaffinity (0, sizeof (*setp), setp) < 0) {
        lua_pushnil (L);
        lua_pushfstring (L, "sched_getaffinity: %s",  strerror (errno));
        return (2);
    }
    return (1);
}

int l_setaffinity (lua_State *L)
{
    cpu_set_t *setp = lua_to_cpu_setp (L, 1);

    if (setp == NULL)
        return 2; /* failed to get cpu_set object */

    if (sched_setaffinity (0, sizeof (*setp), setp) < 0) {
        lua_pushnil (L);
        lua_pushfstring (L, "sched_setaffinity: %s", strerror (errno));
        return (2);
    }
    lua_pushboolean (L, 1);
    return (1);
}

#if !defined LUA_VERSION_NUM || LUA_VERSION_NUM==501
/*
** Adapted from Lua 5.2.0
*/
static void luaL_setfuncs (lua_State *L, const luaL_Reg *l, int nup) {
    luaL_checkstack(L, nup+1, "too many upvalues");
    for (; l->name != NULL; l++) {  /* fill the table with given functions */
        int i;
        lua_pushstring(L, l->name);
        for (i = 0; i < nup; i++)  /* copy upvalues to the top */
            lua_pushvalue(L, -(nup+1));
        lua_pushcclosure(L, l->func, nup);  /* closure with those upvalues */
        lua_settable(L, -(nup + 3));
    }
    lua_pop(L, nup);  /* remove upvalues */
}
#endif

static const struct luaL_Reg cpu_set_functions [] = {
    { "new",        l_cpu_set_new       },
    { "union",      l_cpu_set_union     },
    { "intersect",  l_cpu_set_intersect },
    { NULL,         NULL                },
};

static const struct luaL_Reg cpu_set_methods [] = {
    { "__eq",       l_cpu_set_equals    },
    { "__len",      l_cpu_set_count     },
    { "__add",      l_cpu_set_add       },
    { "__sub",      l_cpu_set_subtract  },
    { "__concat",   l_cpu_set_strconcat },
    { "__tostring", l_cpu_set_tostring  },
    { "__index",    l_cpu_set_index     },
    { "__newindex", l_cpu_set_newindex  },
    { "set",        l_cpu_set_set       },
    { "clr",        l_cpu_set_delete    },
    { "isset",      l_cpu_set_isset     },
    { "zero",       l_cpu_set_zero      },
    { "count",      l_cpu_set_count     },
    { "weight",     l_cpu_set_count     },
    { "union",      l_cpu_set_union     },
    { "is_in",      l_cpu_set_is_in     },
    { "contains",   l_cpu_set_contains  },
    { "intersect",  l_cpu_set_intersect },
    { "iterator",   l_cpu_set_iterate   },
    { "expand",     l_cpu_set_expand    },
    { "copy",       l_cpu_set_copy      },
    { "first",      l_cpu_set_first     },
    { "last",       l_cpu_set_last      },
    { "tohex",      l_cpu_set_tohex     },
    { NULL,         NULL                },
};

static const struct luaL_Reg affinity_functions [] = {
    { "getaffinity", l_getaffinity      },
    { "setaffinity", l_setaffinity      },
    { NULL,          NULL               },
};

int luaopen_affinity (lua_State *L)
{
    luaL_newmetatable (L, "CpuSet");
    luaL_setfuncs (L, cpu_set_methods, 0);

    /*
     *   Create the 'cpuset' subtable for affinity table.
     *     Add cpuset.SETSIZE member, then cpu_set_functions:
     */
    lua_newtable (L);
    lua_pushnumber (L, CPU_SETSIZE);
    lua_setfield (L, -2, "SETSIZE");
    luaL_setfuncs (L, cpu_set_functions, 0);

    lua_newtable (L);
    luaL_setfuncs (L, affinity_functions, 0);

    lua_pushvalue (L, -2);
    lua_setfield (L, -2, "cpuset");

    return 1;
}

/*
 *  vi: ts=4 sw=4 expandtab
 */
