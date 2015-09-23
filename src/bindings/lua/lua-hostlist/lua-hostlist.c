/*****************************************************************************\
 *  lua-hostlist.c - Lua bindings for LLNL hostlist code
 *****************************************************************************
 *  Copyright (C) 2013, Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory.
 *  Written by Mark Grondona <mgrondona@llnl.gov>
 *
 *  LLNL-CODE-645467 All Rights Reserved.
 *
 *  This file is part of lua-hostlist.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License (as published by the
 *  Free Software Foundation) version 2, dated June 1991.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE. See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with lua-hostlist; if not, see <http://www.gnu.org/licenses>.
\*****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "hostlist.h"

/*############################################################################
 *
 *  Hostlist Lua stack utility functions:
 *
 *##########################################################################*/

/*
 *  Return the hostlist object at index in the Lua stack, or error
 *   and abort.
 */
static hostlist_t lua_tohostlist (lua_State *L, int index)
{
    hostlist_t *hptr = luaL_checkudata (L, index, "Hostlist");
    return (*hptr);
}

/*
 *  Push hostlist hl as userdata onto the Lua stack with its metatable set.
 */
static int push_hostlist_userdata (lua_State *L, hostlist_t hl)
{
    hostlist_t *hlp = lua_newuserdata (L, sizeof (*hlp));

    *hlp = hl;
    luaL_getmetatable (L, "Hostlist");
    lua_setmetatable (L, -2);
    return (1);
}

/*
 *  Create a new hostlist from string `s' and push it onto the top
 *   of the Lua stack as userdata.
 */
static int push_hostlist (lua_State *L, const char *s)
{
    hostlist_t hl = hostlist_create (s);
    if (hl == NULL)
        return luaL_error (L, "Unable to create hostlist");

    return push_hostlist_userdata (L, hl);
}

/*
 *  Just like `push_hostlist' above, but return the new hostlist_t
 *   to the caller.
 */
static hostlist_t lua_hostlist_create (lua_State *L, const char *s)
{
    push_hostlist (L, s);
    return (lua_tohostlist (L, -1));
}

/*
 *  Replace a string at index in the Lua stack with a hostlist
 *   If index is already a hostlist, just return a reference to
 *   that object.
 *
 *  Note that the caller does not need to free the returned hostlist.
 *   The Lua garbage collector will handle this.
 */
static hostlist_t lua_string_to_hostlist (lua_State *L, int index)
{
    const char *s;
    hostlist_t hl;

    if (lua_isuserdata (L, index))
        return lua_tohostlist (L, index);

    /*
     *  Create a new hostlist on top of stack
     */
    s = luaL_checkstring (L, index);
    hl = lua_hostlist_create (L, s);

    /*
     *  Replace the string at index with this hostlist
     */
    lua_replace (L, index);
    return (hl);
}

/*
 *  Wrapper for hostlist_delete() to remove up to [limit] occurrences
 *   (limit <= 0 implies unlimited) of hosts in hostlist [del] from
 *   hostlist [hl].
 */
static int hostlist_remove_list (hostlist_t hl, hostlist_t del, int limit)
{
    hostlist_iterator_t i = hostlist_iterator_create (del);
    char *host;

    while ((host = hostlist_next (i))) {
        /*
         *  Delete up to limit occurences of host in list (0 == unlimited)
         */
        int n = limit;
        /* If n == 0 then (--n) should never evaluate true */
        while (hostlist_delete_host (hl, host) && --n) {;}
        free (host);
    }
    hostlist_iterator_destroy (i);
    return (0);
}


/*############################################################################
 *
 *  Hostlist library methods:
 *
 *##########################################################################*/

static int l_hostlist_new (lua_State *L)
{
    push_hostlist (L, lua_tostring (L, 1));
    /*
     *  If a  string was at postion 1,
     *   replace it with the new hostlist
     */
    if (lua_gettop (L) > 1)
        lua_replace (L, 1);
    return (1);
}

/*
 *  This is the hostlist userdata garbage collector method:
 */
static int l_hostlist_destroy (lua_State *L)
{
    hostlist_t hl = lua_tohostlist (L, 1);
    lua_pop (L, 1);
    hostlist_destroy (hl);
    return (0);
}

static int l_hostlist_nth (lua_State *L)
{
    hostlist_t hl;
    char *host;
    int i = lua_tonumber (L, 2);

    i = lua_tonumber (L, 2);
    lua_pop (L, 1);
    /*
     *  If a string is at the top of stack instead of a hostlist,
     *  try to create a hostlist out of it.
     */
    if (lua_isstring (L, 1))
        l_hostlist_new (L);

    hl = lua_tohostlist (L, 1);
    lua_pop (L, 1);

    /*
     *  If index == 0 or is outside of the range of this hostlist,
     *   return nil
     */
    if ((i == 0) || (abs(i) > hostlist_count (hl))) {
        lua_pushnil (L);
        return (1);
    }

    /*
     *  -i indexes from end of list
     */
    if (i < 0)
        i += hostlist_count (hl) + 1;

    host = hostlist_nth (hl, i-1);

    lua_pushstring (L, host);
    free (host);
    return (1);
}

static int l_hostlist_count (lua_State *L)
{
    hostlist_t hl = lua_string_to_hostlist (L, 1);
    lua_pop (L, 1);
    lua_pushnumber (L, hostlist_count (hl));
    return (1);
}

static int l_hostlist_pop (lua_State *L)
{
    hostlist_t hl = lua_tohostlist (L, 1);
    int shift = 0;
    int n = 1;
    int i, t;

    if (lua_gettop (L) > 2)
        return luaL_error (L,
                "Expected 2 arguments to hostlist.pop (got %d)\n");

    if (lua_isnumber (L, 2))
        n = lua_tonumber (L, 2);

    /*
     *  Create table on top of stack for results of pop
     */
    lua_newtable (L);
    t = lua_gettop (L);

    if (n < 0) {
        shift = 1;
        n = abs(n);
    }

    for (i = 0; i < n; i++) {
        char *host;
        if (shift)
            host = hostlist_shift (hl);
        else
            host = hostlist_pop (hl);
        if (host == NULL)
            break;
        lua_pushstring (L, host);
        lua_rawseti (L, t, i+1);
        free (host);
    }
    return (1);
}


static int l_hostlist_remove_n (lua_State *L)
{
    int limit = 0;
    hostlist_t hl, del;

    hl = lua_string_to_hostlist (L, 1);
    del = lua_string_to_hostlist (L, 2);

    /*
     *  Check for a numeric 3rd argument, which is the
     *   total number of entries to delete (in case there are
     *   duplicate hosts in the hostlist).
     */
    if (lua_gettop (L) == 3)
        limit = luaL_checknumber (L, 3);

    hostlist_remove_list (hl, del, limit);

    /*
     *  Return a reference to the original hostlist
     */
    lua_settop (L, 1);
    return (1);
}

/*
 *  As above, but takes variable number of args,
 *   always remove all hosts
 */
static int l_hostlist_remove (lua_State *L)
{
    hostlist_t hl = lua_string_to_hostlist (L, 1);
    int argc = lua_gettop (L);
    int i;

    for (i = 2; i < argc+1; i++) {
        hostlist_t del = lua_string_to_hostlist (L, i);
        hostlist_remove_list (hl, del, 0);
    }

    /*  Remove all args but the hostlist
     */
    lua_settop (L, 1);
    return (1);
}

static int l_hostlist_find (lua_State *L)
{
    hostlist_t hl;
    const char *host;
    int found;

    /*
     *   hostlist.find (hostlist, hostname)
     */
    hl = lua_string_to_hostlist (L, 1);
    host = luaL_checkstring (L, 2);

    found = hostlist_find (hl, host);
    lua_pop (L, 2);

    /*
     *  Return nil if hostlist not found, otherwise return
     *   the index of the first match. (Remember: lua index starts
     *   from 1 not 0.
     */
    if (found < 0)
        lua_pushnil (L);
    else
        lua_pushnumber (L, found+1);

    return (1);
}

static int l_hostlist_concat (lua_State *L)
{
    hostlist_t hl = lua_string_to_hostlist (L, 1);
    int i;
    int argc = lua_gettop (L);

    for (i = 2; i < argc+1; i++) {
        if (lua_isuserdata (L, i))
            hostlist_push_list (hl, lua_tohostlist (L, i));
        else
            hostlist_push (hl, luaL_checkstring (L, i));
    }

    /*
     *  Clean up stack and return original hostlist
     */
    lua_settop (L, 1);
    lua_gc (L, LUA_GCCOLLECT, 0);
    return (1);
}

/*
 *  Core function for Intersect and XOR
 */
static void
hostlist_push_set_result (hostlist_t r, hostlist_t h1,  hostlist_t h2, int xor)
{
    char *host;
    hostlist_iterator_t i = hostlist_iterator_create (h1);

    while ((host = hostlist_next (i))) {
        int found = (hostlist_find (h2, host) >= 0);
        if ((xor && !found) || (!xor && found))
            hostlist_push_host (r, host);
        free (host);
    }
    hostlist_iterator_destroy (i);
}

/*
 *  Perform hostlist intersection or xor (symmetric difference)
 *   against the top two hostlist objects on the stack (promoting
 *   strings to hostlist where necessary).
 */
static int l_hostlist_set_op (lua_State *L, int xor)
{
    int i;
    hostlist_t r;
    int nargs = lua_gettop (L);

    /*
     *  Create hostlist to hold result and push first arg
     */
    r = hostlist_create (NULL);
    hostlist_push_list (r, lua_string_to_hostlist (L, 1));

    /*
     *  Now incrementally build results in r for each arg (2..n)
     */
    for (i = 2; i <= nargs ; i++) {
        hostlist_t hl = lua_string_to_hostlist (L, i);
        hostlist_t tmp = hostlist_create (NULL);

        hostlist_push_set_result (tmp, r, hl, xor);

        /*
         *  For xor we need to also do reverse order
         */
        if (xor)
            hostlist_push_set_result (tmp, hl, r, xor);

        /*
         *   tmp is the new r
         */
        hostlist_destroy (r);
        r = tmp;
    }

    /*
     *  Pop everything and collect garbage
     */
    lua_pop (L, 0);
    lua_gc (L, LUA_GCCOLLECT, 0);

    /*
     *  Always sort and uniq return hostlist
     */
    hostlist_uniq (r);
    push_hostlist_userdata (L, r);
    return (1);
}


static int l_hostlist_intersect (lua_State *L)
{
    return l_hostlist_set_op (L, 0);
}
static int l_hostlist_xor (lua_State *L)
{
    return l_hostlist_set_op (L, 1);
}


/*
 *  hostlist_del: delete all occurence of hosts from first argument
 *   Returns a new hostlist object and doesn't modify the first arg
 */
static int l_hostlist_del (lua_State *L)
{
    hostlist_t r;
    int i;
    int nargs = lua_gettop (L);

    /*
     *   del = (((hl1 - hl2) - hl3) - ... )
     *
     *   Start with first list and remove all others
     */
    r = hostlist_create (NULL);
    hostlist_push_list (r, lua_string_to_hostlist (L, 1));

    for (i = 2; i <= nargs; i++)
        hostlist_remove_list (r, lua_string_to_hostlist (L, i), 0);

    /*
     *  Pop everything and return r
     */
    lua_pop (L, 0);
    lua_gc (L, LUA_GCCOLLECT, 0);

    push_hostlist_userdata (L, r);
    return (1);
}

static int l_hostlist_union (lua_State *L)
{
    hostlist_t r;
    int i;
    int nargs = lua_gettop (L);

    r = hostlist_create (NULL);

    for (i = 1; i <= nargs; i++)
        hostlist_push_list (r, lua_string_to_hostlist (L, i));

    lua_pop (L, 0);
    lua_gc (L, LUA_GCCOLLECT, 0);

    hostlist_uniq (r);
    push_hostlist_userdata (L, r);

    return (1);
}

static int l_hostlist_tostring (lua_State *L)
{
    char buf [4096];
    hostlist_t hl = lua_tohostlist (L, -1);
    lua_pop (L, 1);
    hostlist_ranged_string (hl, sizeof (buf), buf);

    lua_pushstring (L, buf);
    return (1);
}

static int l_hostlist_strconcat (lua_State *L)
{
    const char *s;

    if (lua_isuserdata (L, 1)) {
        /*  Remove string (2nd arg) from stack, leaving hostlist on top:
         */
        s = lua_tostring (L, 2);
        lua_pop (L, 1);

        /*  Convert hostlist to string and leave on stack:
         */
        l_hostlist_tostring (L);

        /*  Push string back into 2nd position
         */
        lua_pushstring (L, s);
    }
    else if (lua_isuserdata (L, 2)) {
        /*  If hostlist is already at top of stack, just convert in place
         */
        l_hostlist_tostring (L);
    }

    /*  Now simply concat the two strings at the top of the stack
     *   and return the result.
     */
    lua_concat (L, 2);
    return (1);
}

static int l_hostlist_uniq (lua_State *L)
{
    hostlist_t hl = lua_tohostlist (L, 1);
    hostlist_uniq (hl);
    /*
     *  Return a reference to the hostlist
     */
    return (1);
}

static int l_hostlist_sort (lua_State *L)
{
    hostlist_t hl = lua_tohostlist (L, 1);
    hostlist_sort (hl);
    /*
     *  Return a reference to the hostlist
     */
    return (1);
}

static int l_hostlist_index (lua_State *L)
{
    const char *key = lua_tostring (L, 2);

    if (key == NULL)
        return luaL_error (L, "hostlist: invalid index");

    /*
     *  Check to see if key is a name of a method in the metatable
     *   and return the method if so:
     *
     *  [ Lua will first try to resolve unknown table entries from hostlist
     *     object using __index in the object's metatable. The __index
     *     function should then return the method in question, which is
     *     then called with the supplied arguments. (Thus we can't directly
     *     call the named method here, instead we leave the method reference
     *     on the stack as a cfunction. Normally, if you are not trying to
     *     overload the [] operator, you can assign the metatable's __index
     *     to the metatable itself to get the same effect.) ]
     */
    lua_getmetatable (L, 1);
    lua_getfield (L, -1, key);
    if (!lua_isnil (L, -1))
        return 1;

    /*
     *  Else hl[n] was requested, so return the nth host.
     */
    return (l_hostlist_nth (L));
}

/*
 *  Map a function across all members of a hostlist.
 *   Returns a hostlist. Function argument is required.
 */
static int l_hostlist_map (lua_State *L)
{
    char *host;
    hostlist_t hl, r;
    hostlist_iterator_t i;

    hl = lua_string_to_hostlist (L, 1);
    if (!lua_isfunction (L, 2))
        return luaL_argerror (L, 2, "expected function argument");

    /*  Create new hostlist at top of stack to hold results:
     */
    r = lua_hostlist_create (L, NULL);

    i = hostlist_iterator_create (hl);
    while ((host = hostlist_next (i))) {
        /*  Copy function to run to top of stack to be consumed by lua_pcall
         */
        lua_pushvalue (L, 2);

        /*  Push hostname for arg to fn:
         */
        lua_pushstring (L, host);

        /*
         *  Call function and leave 1 result on the stack
         */
        if (lua_pcall (L, 1, 1, 0) != 0) {
                hostlist_iterator_destroy (i);
                free (host);
                return luaL_error (L, "map: %s", lua_tostring (L, -1));
        }

        /*
         *  Only push entry on hostlist if there was a return value
         */
        if (!lua_isnil (L, -1))
            hostlist_push_host (r, lua_tostring (L, -1));
        lua_pop (L, 1);
        free (host);
    }

    hostlist_iterator_destroy(i);

    /*  Clean up stack and return
     */
    lua_replace (L, 1);
    lua_pop (L, lua_gettop (L) - 1);

    return (1);
}

/*
 *  Return a hostlist as a lua table, optionally applying a function
 *   argument to all hosts.
 */
static int l_hostlist_expand (lua_State *L)
{
    char *host;
    hostlist_t hl;
    hostlist_iterator_t i;
    int has_function;
    int n, t;

    hl = lua_string_to_hostlist (L, 1);

    has_function = lua_isfunction (L, 2);

    /*  Create new table at top of stack to hold results:
     */
    lua_newtable (L);
    t = lua_gettop (L);

    n = 1;
    i = hostlist_iterator_create (hl);
    while ((host = hostlist_next (i))) {
        /*  If we have a function to run, copy onto the top of the stack
         *   to be consumed by lua_pcall
         */
        if (has_function)
            lua_pushvalue (L, 2);

        /*  Push hostname, either as value for the table, or arg to fn:
         */
        lua_pushstring (L, host);

        /*
         *  Call function if needed and leave 1 result on the stack
         */
        if (has_function && lua_pcall (L, 1, 1, 0) != 0) {
                hostlist_iterator_destroy (i);
                free (host);
                return luaL_error (L, "map: %s", lua_tostring (L, -1));
        }

        /*
         *  Only push entry on table if there was a return value
         */
        if (lua_isnil (L, -1))
            lua_pop (L, 1);
        else
            lua_rawseti (L, t, n++);
        free (host);
    }

    hostlist_iterator_destroy(i);

    /*  Clean up stack and return
     */
    lua_replace (L, 1);
    lua_pop (L, lua_gettop (L) - 1);

    return (1);
}


/*
 *  Return a hostlist_iterator_t *  full userdata as hostlist_iterator_t
 */
static hostlist_iterator_t lua_tohostlist_iterator (lua_State *L, int index)
{
    hostlist_iterator_t *ip = luaL_checkudata (L, index, "HostlistIterator");
    return (*ip);
}


static int l_hostlist_iterator_destroy (lua_State *L)
{
    hostlist_iterator_t i = lua_tohostlist_iterator (L, 1);
    hostlist_iterator_destroy (i);
    return (0);
}

/*
 *  Hostlist iterator function (assumed to be a C closure)
 */
static int l_hostlist_iterator (lua_State *L)
{
    char *host;
    /*
     *  Get hostlist iterator instance, which is the sole upvalue
     *   for this closure
     */
    int index = lua_upvalueindex (1);
    hostlist_iterator_t i = lua_tohostlist_iterator (L, index);

    if (i == NULL)
        return luaL_error (L, "Invalid hostlist iterator");

    host = hostlist_next (i);
    if (host != NULL) {
        lua_pushstring (L, host);
        free (host);
        return (1);
    }
    return (0);
}

/*
 *  Create a new iterator (as a C closure)
 */
static int l_hostlist_next (lua_State *L)
{
    hostlist_t hl = lua_tohostlist (L, 1);
    hostlist_iterator_t *ip;

    lua_pop (L, 1);

    /*
     *  Push hostlist iterator onto stack top with metatable set
     */
    ip = lua_newuserdata (L, sizeof (*ip));
    *ip = hostlist_iterator_create (hl);
    luaL_getmetatable (L, "HostlistIterator");
    lua_setmetatable (L, -2);


    /*
     *  Used in for loop, iterator creation function should return:
     *   iterator, state, starting val (nil)
     *    state is nil becuase we are using a closure.
     */
    lua_pushcclosure (L, l_hostlist_iterator, 1);

    return (1);
}

/*############################################################################
 *
 *  Hostlist interface definitions and initialization:
 *
 *##########################################################################*/

static const struct luaL_Reg hostlist_functions [] = {
    { "new",        l_hostlist_new       },
    { "intersect",  l_hostlist_intersect },
    { "xor",        l_hostlist_xor       },
    { "delete",     l_hostlist_remove    },
    { "delete_n",   l_hostlist_remove_n  },
    { "union",      l_hostlist_union     },
    { "map",        l_hostlist_map       },
    { "expand",     l_hostlist_expand    },
    { "nth",        l_hostlist_nth       },
    { "pop",        l_hostlist_pop       },
    { "concat",     l_hostlist_concat    },
    { "find",       l_hostlist_find      },
    { "count",      l_hostlist_count     },
    { NULL,         NULL                 }
};

static const struct luaL_Reg hostlist_methods [] = {
    { "__len",      l_hostlist_count     },
    { "__index",    l_hostlist_index     },
    { "__tostring", l_hostlist_tostring  },
    { "__concat",   l_hostlist_strconcat },
    { "__add",      l_hostlist_union     },
    { "__mul",      l_hostlist_intersect },
    { "__pow",      l_hostlist_xor       },
    { "__sub",      l_hostlist_del       },
    { "__gc",       l_hostlist_destroy   },
    { "count",      l_hostlist_count     },
    { "delete",     l_hostlist_remove    },
    { "delete_n",   l_hostlist_remove_n  },
    { "concat",     l_hostlist_concat    },
    { "uniq",       l_hostlist_uniq      },
    { "sort",       l_hostlist_sort      },
    { "next",       l_hostlist_next      },
    { "map",        l_hostlist_map       },
    { "expand",     l_hostlist_expand    },
    { "pop",        l_hostlist_pop       },
    { "find",       l_hostlist_find      },
    { NULL,         NULL                 }
};


/*
 *  Hostlist iterator only needs a garbage collection method,
 *   it is not otherwise exposed to Lua scripts.
 */
static const struct luaL_Reg hostlist_iterator_methods [] = {
    { "__gc",       l_hostlist_iterator_destroy   },
    { NULL,         NULL                          }
};


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

int luaopen_hostlist (lua_State *L)
{
    luaL_newmetatable (L, "Hostlist");
    /*  Register hostlist methods in metatable */
    luaL_setfuncs (L, hostlist_methods, 0);

    luaL_newmetatable (L, "HostlistIterator");
    luaL_setfuncs (L, hostlist_iterator_methods, 0);

    /*  Register hostlist public table functions: */
    lua_newtable (L);
    luaL_setfuncs (L, hostlist_functions, 0);

    return 1;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
