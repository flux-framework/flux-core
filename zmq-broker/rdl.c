#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "rdl.h"
#include "util/list.h"
#include "dlua/json-lua.h"

#define VERR(r,args...) (*((r)->errf)) ((r)->errctx, args)

static rdl_err_f default_err_f = NULL;
static void *    default_err_ctx = NULL;

struct rdllib {
    lua_State *L;          /*  Global lua state                        */
    rdl_err_f  errf;       /*  Error/debug function                    */
    void *     errctx;     /*  ctx passed to error/debug function      */
    List       rdl_list;   /*  List of rdl db instances                */
};

/*
 *  Single RDL instance.
 */
struct rdl {
    struct rdllib *rl;     /*  Pointer back to rdllib owning instance  */
    lua_State *L;          /*  Local Lua state                         */
    int        lua_ref;    /*  Reference in global Lua registry        */
    List       resource_list;  /*  List of active resource references  */
};

/*
 *  Handle to a resource representation inside an rdl instance
 */
struct resource {
    struct rdl *rdl;       /*  Reference back to 'owning' rdl instance  */
    int         lua_ref;   /*  Lua reference into  rdl->L globals table */
    char *      name;      /*  Copy of resource name (on first use)     */
    char *      path;      /*  Copy of resource path (on first use)     */
};

/*
 *   Resource accumulator
 */
struct rdl_accumulator {
    struct rdl *rdl;
    int         lua_ref;   /*  Lua reference into  rdl->L globals table */
};


/***************************************************************************
 *  Static functions
 ***************************************************************************/

static void verr (void *ctx, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
}

void lsd_fatal_error (char *file, int line, char *msg)
{
    verr (NULL, msg);
    exit (1);
}

void * lsd_nomem_error (char *file, int line, char *msg)
{
    verr (NULL, "Out of memory: %s: %s:%d\n", msg, file, line);
    return NULL;
}

void rdllib_close (struct rdllib *rl)
{
    if (rl == NULL)
        return;
    if (rl->rdl_list)
        list_destroy (rl->rdl_list);
    if (rl->L)
        lua_close (rl->L);
    free (rl);
}

static int rdllib_init (struct rdllib *rl)
{
    /* XXX: Is there no equivalent from C? */
    lua_getglobal (rl->L, "require");
    lua_pushstring (rl->L, "RDL");
    if (lua_pcall (rl->L, 1, 1, 0)) {
        VERR (rl, "loading RDL: %s\n", lua_tostring (rl->L, -1));
        return (-1);
    }
    if (!lua_istable (rl->L, -1)) {
        VERR (rl, "Failed to load RDL: %s\n", lua_tostring (rl->L, -1));
    }
    /*
     *  Assign implementation to global RDL table.
     */
    lua_setglobal (rl->L, "RDL");

    lua_settop (rl->L, 0);
    return (0);
}

static int ptrcmp (void *x, void *y)
{
    return (x == y);
}

static void rdllib_rdl_delete (struct rdllib *l, struct rdl *rdl)
{
    if (l->rdl_list)
        list_delete_all (l->rdl_list, (ListFindF) ptrcmp, rdl);
}

static void rdl_free (struct rdl *rdl)
{
   if (rdl == NULL)
        return;
    if (rdl->resource_list)
        list_destroy (rdl->resource_list);
    if (rdl->L && rdl->rl && rdl->rl->L) {
        /*
         *  unref this Lua thread state from global library Lua state
         */
        luaL_unref (rdl->rl->L, LUA_REGISTRYINDEX, rdl->lua_ref);

        /*
         *  Only call lua_close() on global/main rdllib Lua state:
         */
        rdl->L = NULL;
        rdl->rl = NULL;
    }
    free (rdl);
}

struct rdllib * rdllib_open (void)
{
    struct rdllib *rl = malloc (sizeof (*rl));
    if (rl == NULL)
        return NULL;

    rl->L = luaL_newstate ();
    if (rl->L == NULL) {
        rdllib_close (rl);
        return NULL;
    }

    luaL_openlibs (rl->L);
    rl->errf = default_err_f ? default_err_f : &verr;
    rl->errctx = default_err_ctx;

    rl->rdl_list = list_create ((ListDelF) rdl_free);
    if (rl->rdl_list == NULL) {
        rdllib_close (rl);
        return (NULL);
    }

    if (rdllib_init (rl) < 0)
        return NULL;

    return (rl);
}

int rdllib_set_errf (struct rdllib *l, void *ctx, rdl_err_f fn)
{
    l->errf = fn;
    l->errctx = ctx;
    return (0);
}

void rdllib_set_default_errf (void *ctx, rdl_err_f fn)
{
    default_err_ctx = ctx;
    default_err_f = fn;
}

void rdl_destroy (struct rdl *rdl)
{
    if (rdl && rdl->rl)
        rdllib_rdl_delete (rdl->rl, rdl);
    else
        rdl_free (rdl);
}

static int rdl_dostringf (struct rdl *rdl, const char *fmt, ...)
{
    char *s;
    int rc;
    int top;

    va_list ap;
    va_start (ap, fmt);
    rc = vasprintf (&s, fmt, ap);
    va_end (ap);

    if (rc < 0)
        return (-1);

    top = lua_gettop (rdl->L);

    if (  luaL_loadstring (rdl->L, s)
       || lua_pcall (rdl->L, 0, LUA_MULTRET, 0)) {
        VERR (rdl->rl, "dostring (%s): %s\n", s, lua_tostring (rdl->L, -1));
        lua_settop (rdl->L, 0);
        free (s);
        return (-1);
    }
    free (s);

    return (lua_gettop (rdl->L) - top);
}

#if 0
static int string_is_serialized_rdl (const char *s)
{
    int rc = strncmp (s, RDL_SERIALIZED_HEADER, strlen (RDL_SERIALIZED_HEADER));
    return (rc == 0);
}
static int rdl_evaluate_serialized (struct rdl *rdl)
{
    /*
     *  Serialized RDL should return a new table representing
     *   the rdl db. We just have to assign the appropriate metatable
     *   to have a working rdl implementation.
     *
     *   XXX: Sandbox?
     */
    if (lua_pcall (rdl->L, 0, LUA_MULTRET, 0)) {
        (*rdl->rl->errf) ("rdl_load: run: %s\n", lua_tostring (rdl->L, -1));
        return (-1);
    }
    if (lua_gettop (rdl->L) != 1) {
        (*rdl->rl->errf) ("rdl_load: Failed to find rdl table\n");
        return (-1);
    }
    if (lua_type (rdl->L, -1) != LUA_TTABLE) {
        (*rdl->rl->errf) ("rdl_load: must return table\n");
        return (-1);
    }
    lua_setglobal (rdl->L, "rdl");
lua_getglobal (rdl->L, "rdl");

    rdl_dostringf (rdl, "return RDL.memstore");
    lua_setmetatable (rdl->L, -2);


    return (1);
}

static int rdl_evaluate_config (struct rdl *rdl)
{
    int envref;
    /*
     *  Set Lua env for RDL chunk
     */
    if (rdl_dostringf (rdl, "return RDL.envtable()") < 1) {
        (*rdl->rl->errf) ("Failed to get RDL.envtable: %s\n",
            lua_tostring (rdl->L, -1));
        return (-1);
    }
    /*  Save a ref to environment table */
    lua_pushvalue (rdl->L, -1);
    envref = luaL_ref (rdl->L, LUA_REGISTRYINDEX);

    /*  Set env table as environment for rdl chunk: */
    lua_setfenv (rdl->L, -2);

    /*
     *  Now run the chunk at top of stack:
     */
    if (lua_pcall (rdl->L, 0, 0, 0)) {
        (*rdl->rl->errf) ("rdl_load: run: %s\n", lua_tostring (rdl->L, -1));
        return (-1);
    }

    /*
     *  Get copy of environment and copy 'rdl' table to global variable:
     */
    lua_rawgeti (rdl->L, LUA_REGISTRYINDEX, envref);

    lua_pushliteral (rdl->L, "rdl");
    lua_rawget (rdl->L, -2);
    lua_setglobal (rdl->L, "rdl");

    luaL_unref (rdl->L, LUA_REGISTRYINDEX, envref);

    return (1);
}
#endif

/*
 *  Create a new Lua thread of execution with its own global state,
 *   keeping the main Lua global table as read-only fallback.
 */
static lua_State *create_thread (lua_State *globalL)
{
    lua_State *L = lua_newthread (globalL);

    /*  Now, set up metatable where __index = globalL._G */
    lua_newtable (L);
    lua_newtable (L);
    lua_pushliteral (L, "__index");
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_settable(L, -3);
    lua_setmetatable(L, -2);

    /*
     *  Replace current thread's globals index with
     *   newly created empty table
     */
    lua_replace (L, LUA_GLOBALSINDEX);
    return (L);
}

static void rdl_resource_delete (struct rdl *rdl, struct resource *r)
{
    if (!rdl->resource_list)
        return;
    list_delete_all (rdl->resource_list, (ListFindF) ptrcmp, r);
}

static void rdl_resource_free (struct resource *r)
{
    if (r->rdl->L) {
        luaL_unref (r->rdl->L, LUA_GLOBALSINDEX, r->lua_ref);
        r->rdl = NULL;
    }
    free (r->name);
    free (r->path);
    free (r);
}

void rdl_resource_destroy (struct resource *r)
{
    if (r->rdl && r->rdl->resource_list)
        rdl_resource_delete (r->rdl, r);
    else
        rdl_resource_free (r);
}

/*
 *  Allocate a new RDL instance under library state [rl].
 */
static struct rdl * rdl_new (struct rdllib *rl)
{
    struct rdl * rdl = malloc (sizeof (*rdl));
    if (rdl == NULL)
        return NULL;
    /*
     *  Each rdl instance is a new thread in global lua state rl->L
     */
    rdl->L = create_thread (rl->L);
    rdl->lua_ref = luaL_ref (rl->L, LUA_REGISTRYINDEX);

    if (!rdl->L || rdl->lua_ref == LUA_NOREF) {
        rdl_destroy (rdl);
        return (NULL);
    }

    /* Pointer back to 'library'instance */
    rdl->rl = rl;
    rdl->resource_list = list_create ((ListDelF) rdl_resource_free);

    /* Leave rdl instance on top of stack */
    return (rdl);
}

/*
 *  Pop an RDL table from source lua thread in [from] and move it
 *   to a newly created lua state, returning the new RDL instance.
 */
static struct rdl * lua_pop_new_rdl (struct rdl *from)
{
    struct rdl *to;
    /*
     *  Ensure item at top of stack is at least a table:
     */
    if (lua_type (from->L, -1) != LUA_TTABLE)
        return (NULL);

    /*
     *  Create a new rdl object within this library state:
     */
    to = rdl_new (from->rl);

    lua_xmove (from->L, to->L, -1);
    lua_setglobal (to->L, "rdl");
    return (to);
}

static struct rdl * loadfn (struct rdllib *rl, const char *fn, const char *s)
{
    int rc;
    struct rdl * rdl = rdl_new (rl);
    if (rdl == NULL)
        return NULL;

    /*
     *  First, get function to evaluate rdl:
     */
    rc = rdl_dostringf (rdl, "return require 'RDL'.%s", fn);
    if (rc <= 0) {
        VERR (rl, "rdl_load: Failed to get function RDL.%s\n", fn);
        rdl_destroy (rdl);
        return (NULL);
    }

    /*
     *  Now push function arg `s' onto stack, and evaluate the function:
     */
    if (s)
        lua_pushstring (rdl->L, s);
    if (lua_pcall (rdl->L, s?1:0, LUA_MULTRET, 0)) {
        VERR (rl, "rdl_load: RDL.%s: %s\n", fn, lua_tostring (rdl->L, -1));
        rdl_destroy (rdl);
        return (NULL);
    }

    if (lua_type (rdl->L, -1) != LUA_TTABLE) {
        VERR (rl, "rdl_load: %s\n", lua_tostring (rdl->L, -1));
        rdl_destroy (rdl);
        return (NULL);
    }
    lua_setglobal (rdl->L, "rdl");
    lua_settop (rdl->L, 0);

    list_append (rl->rdl_list, rdl);
    lua_settop (rdl->L, 0);
    return (rdl);
}

struct rdl * rdl_loadfile (struct rdllib *rl, const char *file)
{
    return loadfn (rl, "evalf", file);
}

struct rdl * rdl_load (struct rdllib *rl, const char *s)
{
    return loadfn (rl, "eval", s);
}


struct rdl * rdl_copy (struct rdl *rdl)
{
    if (rdl == NULL)
        return (NULL);
    assert (rdl->L);
    assert (rdl->rl);
    /*
     *  Call memstore:dup() function to push copy of current rdl
     *   onto stack:
     */
    rdl_dostringf (rdl, "return rdl:dup()");
    return (lua_pop_new_rdl (rdl));
}

static int lua_rdl_push (struct rdl *rdl)
{
    lua_rawgeti (rdl->L, LUA_GLOBALSINDEX, rdl->lua_ref);
    return (1);
}

static int lua_rdl_method_push (struct rdl *rdl, const char *name)
{
    lua_State *L = rdl->L;
    /*
     *  First push rdl resource proxy object onto stack
     */
    lua_rdl_push (rdl);
    lua_pushstring (L, name);
    lua_rawget (L, -2);

    if (lua_type (L, -1) != LUA_TFUNCTION) {
        lua_pushnil (L);
        lua_pushstring (L, "not a method");
        return (-1);
    }

    /*
     *  Push rdl reference again as first argument to "Method"
     */
    lua_rdl_push (rdl);
    return (0);
}

struct rdl * rdl_find (struct rdl *rdl, json_object *args)
{
    lua_rdl_method_push (rdl, "find");

    if (json_object_to_lua (rdl->L, args) < 0) {
        VERR (rdl->rl, "Failed to convert JSON to Lua\n");
        return (NULL);
    }
    /*
     *  stack: [ Method, object, args-table ]
     */
    if (lua_pcall (rdl->L, 2, LUA_MULTRET, 0) || lua_isnoneornil (rdl->L, 1)) {
        VERR (rdl->rl, "find(%s): %s\n",
                json_object_to_json_string (args),
                lua_tostring (rdl->L, -1));
        return (NULL);
    }

    return (lua_pop_new_rdl (rdl));
}

char * rdl_serialize (struct rdl *rdl)
{
    char *s;
    if (rdl == NULL)
        return (NULL);
    assert (rdl->L);
    assert (rdl->rl);

    rdl_dostringf (rdl, "return rdl:serialize()");
    asprintf (&s, "%s\n%s", "-- RDL v1.0", lua_tostring (rdl->L, -1));
    lua_settop (rdl->L, 0);
    return s;
}

static struct resource * create_resource_ref (struct rdl *rdl, int index)
{
    struct resource *r;
    r = malloc (sizeof (*r));
    r->lua_ref = luaL_ref (rdl->L, LUA_GLOBALSINDEX);
    r->rdl = rdl;
    r->path = NULL;
    r->name = NULL;
    list_append (rdl->resource_list, r);
    return (r);
}

struct resource * rdl_resource_get (struct rdl *rdl, const char *uri)
{
    struct resource *r;
    if (uri == NULL)
        uri = "default";
    rdl_dostringf (rdl, "return rdl:resource ('%s')", uri);
    if (lua_type (rdl->L, -1) != LUA_TTABLE) {
        VERR (rdl->rl, "resource (%s): %s\n", uri, lua_tostring (rdl->L, -1));
        return (NULL);
    }
    r = create_resource_ref (rdl, -1);
    lua_settop (rdl->L, 0);
    return (r);
}

static int lua_rdl_resource_push (struct resource *r)
{
    lua_rawgeti (r->rdl->L, LUA_GLOBALSINDEX, r->lua_ref);
    return (1);
}

static int lua_rdl_resource_method_push (struct resource *r, const char *name)
{
    lua_State *L = r->rdl->L;
    /*
     *  First push rdl resource proxy object onto stack
     */
    lua_rdl_resource_push (r);
    lua_getfield (L, -1, name);

    if (lua_type (L, -1) != LUA_TFUNCTION) {
        lua_pop (L, 1);
        lua_pushnil (L);
        lua_pushstring (L, "not a method");
        return (-1);
    }

    /*
     *  Push rdl resource reference again as first argument to "Method"
     */
    lua_rdl_resource_push (r);
    return (0);
}

static int lua_rdl_resource_getfield (struct resource *r, const char *x)
{
    lua_State *L = r->rdl->L;
    lua_rdl_resource_push (r);
    lua_getfield (L, -1, x);
    if (lua_isnoneornil (L, -1))
        return (-1);
    lua_replace (L, -2);
    return (0);
}

int lua_rdl_resource_method_call (struct resource *r, const char *name)
{
    if (lua_rdl_resource_method_push (r, name) < 0)
        return (-1);
    return lua_pcall (r->rdl->L, 1, LUA_MULTRET, 0);
}

/*
 *  For resource name and path, be cowardly and reread from Lua for
 *   each call. In the future, we may want to cache these values.
 */
const char * rdl_resource_name (struct resource *r)
{
    lua_State *L = r->rdl->L;

    if (lua_rdl_resource_getfield (r, "name") < 0)
        return (NULL);
    if (r->name)
        free (r->name);
    r->name = strdup (lua_tostring (L, -1));
    lua_pop (L, 1);
    return (r->name);
}

const char * rdl_resource_path (struct resource *r)
{
    lua_State *L = r->rdl->L;

    if (lua_rdl_resource_getfield (r, "path") < 0)
        return (NULL);
    if (r->path)
        free (r->path);
    r->path = strdup (lua_tostring (L, -1));
    lua_pop (L, 1);
    return (r->path);
}

static int rdl_resource_method_call1_keepstack (struct resource *r,
    const char *method, const char *arg)
{
    int rc = 0;
    lua_State *L = r->rdl->L;
    if (lua_rdl_resource_method_push (r, method) < 0)
        return (-1);
    lua_pushstring (L, arg);
    /*
     *  stack: [ Method, object, arg ]
     */
    if (lua_pcall (L, 2, LUA_MULTRET, 0) || lua_isnoneornil (L, 1)) {
        VERR (r->rdl->rl, "%s(%s): %s\n", method, arg, lua_tostring (L, -1));
        lua_settop (L, 0);
        rc = -1;
    }
    return (rc);
}

static int rdl_resource_method_call1 (struct resource *r,
    const char *method, const char *arg)
{
    int rc = rdl_resource_method_call1_keepstack (r, method, arg);
    lua_settop (r->rdl->L, 0);
    return (rc);
}

void rdl_resource_tag (struct resource *r, const char *tag)
{
    rdl_resource_method_call1 (r, "tag", tag);
}

void rdl_resource_delete_tag (struct resource *r, const char *tag)
{
    if (rdl_resource_method_call1 (r, "delete_tag", tag) < 0) {
        VERR (r->rdl->rl, "delete_tag (%s): %s\n", tag,
              lua_tostring (r->rdl->L, -1));
    }
}

int rdl_resource_set_int (struct resource *r, const char *tag, int64_t val)
{
    int rc = 0;
    lua_State *L = r->rdl->L;

    if (lua_rdl_resource_method_push (r, "tag") < 0)
        return (-1);
    lua_pushstring (L, tag);
    lua_pushnumber (L, val);
    if (lua_pcall (L, 3, LUA_MULTRET, 0) || lua_isnoneornil (L, 1)) {
        VERR (r->rdl->rl, "%s(%s): %s\n", "tag", tag, lua_tostring (L, -1));
        rc = -1;
    }
    lua_settop (L, 0);
    return (rc);
}

int rdl_resource_get_int (struct resource *r, const char *tag, int64_t *valp)
{
    lua_State *L = r->rdl->L;
    if (rdl_resource_method_call1_keepstack (r, "get", tag)  < 0)
        return (-1);
    *valp = (int64_t) lua_tointeger (L, -1);
    lua_settop (L, 0);
    return (0);
}

int rdl_resource_unlink_child (struct resource *r, const char *name)
{
    return rdl_resource_method_call1 (r, "unlink", name);
}

/*
 *  Call [method] on resource [r] and return resulting Lua table
 *   as a json-c json_object.
 */
static json_object *
rdl_resource_method_to_json (struct resource *r, const char *method)
{
    json_object *o = NULL;
    lua_State *L = r->rdl->L;

    if (lua_rdl_resource_method_call (r,  method)) {
        VERR (r->rdl->rl, "json: %s\n", lua_tostring (L, -1));
        return (NULL);
    }
    if (lua_type (L, -1) != LUA_TTABLE) {
        VERR (r->rdl->rl, "json: Failed to get table. Got %s\n",
                             luaL_typename (L, -1));
        lua_pop (L, 1);
        return (NULL);
    }
    if (lua_value_to_json (L, -1, &o) < 0)
        o = NULL;

    /* Keep Lua stack clean */
    lua_settop (L, 0);
    return (o);
}

json_object * rdl_resource_json (struct resource *r)
{
    return rdl_resource_method_to_json (r, "tabulate");
}

json_object * rdl_resource_aggregate_json (struct resource *r)
{
    return rdl_resource_method_to_json (r, "aggregate");
}


struct resource * rdl_resource_next_child (struct resource *r)
{
    struct resource *c;
    if (lua_rdl_resource_method_call (r, "next_child")) {
        VERR (r->rdl->rl, "next child: %s\n", lua_tostring (r->rdl->L, -1));
        return NULL;
    }
    if (lua_isnil (r->rdl->L, -1)) {
        /* End of child list is indicated by nil return */
        return (NULL);
    }
    c = create_resource_ref (r->rdl, -1);
    lua_settop (r->rdl->L, 0);
    return (c);
}


void rdl_resource_iterator_reset (struct resource *r)
{
    if (lua_rdl_resource_method_call (r, "reset"))
        VERR (r->rdl->rl, "iterator reset: %s\n", lua_tostring (r->rdl->L, -1));
}

/*
 *  RDL Accumulator methods:
 */
void rdl_accumulator_destroy (struct rdl_accumulator *a)
{
    if (a == NULL)
        return;

    if (a->rdl) {
        luaL_unref (a->rdl->L, LUA_GLOBALSINDEX, a->lua_ref);
        a->rdl = NULL;
    }
    free (a);
}

struct rdl_accumulator * rdl_accumulator_create (struct rdl *rdl)
{
    struct rdl_accumulator *a;

    rdl_dostringf (rdl, "return rdl:resource_accumulator()");
    if (lua_type (rdl->L, -1) != LUA_TTABLE) {
        VERR (rdl->rl, "accumlator_create: %s\n", lua_tostring (rdl->L, -1));
        return (NULL);
    }
    a = malloc (sizeof (*a));
    a->lua_ref = luaL_ref (rdl->L, LUA_GLOBALSINDEX);
    a->rdl = rdl;
    lua_settop (rdl->L, 0);
    return (a);
}

static int lua_rdl_accumulator_push (struct rdl_accumulator *a)
{
    lua_State *L = a->rdl->L;
    lua_rawgeti (L, LUA_GLOBALSINDEX, a->lua_ref);
    if (lua_type (L, -1) != LUA_TTABLE) {
        return (-1);
    }
    return (0);
}

static int lua_rdl_accumulator_method_push (struct rdl_accumulator *a,
    const char *name)
{
    lua_State *L = a->rdl->L;
    if (lua_rdl_accumulator_push (a) < 0)
        return (-1);
    lua_getfield (L, -1, name);
    lua_replace (L, -2); /* remove accumulator object, replace with method */

    if (lua_type (L, -1) != LUA_TFUNCTION) {
        lua_pushnil (L);
        lua_pushstring (L, "not a method");
        return (-1);
    }

    return (lua_rdl_accumulator_push (a));
}

int rdl_accumulator_add (struct rdl_accumulator *a, struct resource *r)
{
    int rc = 0;
    lua_State *L = a->rdl->L;

    lua_rdl_accumulator_method_push (a, "add");
    if (lua_rdl_resource_getfield (r, "uuid") < 0)
        return (-1);

    /* Stack: [ Method, Object, arg ] */
    if (lua_pcall (L, 2, LUA_MULTRET, 0) || lua_isnoneornil (L, 1)) {
        VERR (a->rdl->rl, "accumulator_add: %s\n", lua_tostring (L, -1));
        rc = -1;
    }
    lua_settop (L, 0);
    return rc;
}

char * rdl_accumulator_serialize (struct rdl_accumulator *a)
{
    char *s;
    lua_State *L = a->rdl->L;
    lua_rdl_accumulator_method_push (a, "serialize");
    if (lua_pcall (L, 1, LUA_MULTRET, 0)) {
        VERR (a->rdl->rl, "accumulator:serialize: %s\n", lua_tostring (L, -1));
        return (NULL);
    }
    asprintf (&s, "-- RDL v1.0\n%s", lua_tostring (L, -1));
    lua_settop (L, 0);
    return (s);
}

struct rdl * rdl_accumulator_copy (struct rdl_accumulator *a)
{
    struct rdl *rdl;
    char *s;

    if (a == NULL)
        return (NULL);

    if ((s = rdl_accumulator_serialize (a)) == NULL) {
        VERR (a->rdl->rl, "serialization failure\n");
        return (NULL);
    }
    rdl = rdl_load (a->rdl->rl, s);
    free (s);
    return (rdl);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
