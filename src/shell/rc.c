/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/
#define FLUX_SHELL_PLUGIN_NAME NULL

/* Load and run shell rc script
 *
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <libgen.h>
#include <glob.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <flux/core.h>
#include <flux/shell.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/bindings/lua/jansson-lua.h"
#include "src/bindings/lua/lutil.h"
#include "ccan/str/str.h"
#include "internal.h"
#include "info.h"

/*  Lua plugin helper types:
 */
struct lua_plugref {
    char *topic;
    int lua_ref;
};

struct lua_plugin {
    char *name;      /*  Registered plugin name                         */
    char *filename;  /*  Lua source filename                            */
    zlistx_t *refs;  /*  List of Lua handler references for this plugin */
};

/*  Global Lua state
 */
static lua_State *global_L = NULL;

/*
 *  Global copy of flux_shell_t object
 */
static flux_shell_t *rc_shell = NULL;

/*  Stack of current Lua filenames
 */
static zlistx_t *file_stack = NULL;



/* Push a filename onto the current stack of Lua files
 */
static void file_stack_push (const char *file)
{
    char *s = strdup (file);
    zlistx_add_start (file_stack, s);
}

/*  Pop most recent Lua file from stack
 */
static void file_stack_pop (void)
{
    if (zlistx_first (file_stack)) {
        void *item = zlistx_detach_cur (file_stack);
        free (item);
    }
}

/*  Return the current file name in the stack
 */
const char *current_file (void)
{
    return zlistx_first (file_stack);
}


static void lua_plugref_destroy (struct lua_plugref *ref)
{
    if (ref) {
        free (ref->topic);
        if (global_L)
            luaL_unref (global_L, LUA_REGISTRYINDEX, ref->lua_ref);
        free (ref);
    }
}

struct lua_plugref *lua_plugref_create (const char *topic, int lua_ref)
{
    struct lua_plugref *ref = calloc (1, sizeof (*ref));
    if (!ref || !(ref->topic = strdup (topic))) {
        lua_plugref_destroy (ref);
        return NULL;
    }
    ref->lua_ref = lua_ref;
    return ref;
}

/*  czmq destructor version of lua_plugref_destroy
 */
static void plugref_destroy (void **item)
{
    if (item) {
        lua_plugref_destroy (*item);
        *item = NULL;
    }
}

static void lua_plugin_destroy (struct lua_plugin *lp)
{
    if (lp) {
        free (lp->name);
        free (lp->filename);
        zlistx_destroy (&lp->refs);
        free (lp);
    }
}

static struct lua_plugin *lua_plugin_create (const char *filename)
{
    struct lua_plugin *lp = calloc (1, sizeof (*lp));
    if (!lp)
        return NULL;
    lp->filename = strdup (filename);
    lp->refs = zlistx_new ();
    zlistx_set_destructor (lp->refs, plugref_destroy);
    return lp;
}

/*  Handler for all Lua plugin callbacks.
 *
 *  Grabs Lua handler function from registry, push args on stack
 *   (currently just actual topic string) and call handler.
 *
 */
static int lua_plugin_cb (flux_plugin_t *p,
                          const char *topic,
                          flux_plugin_arg_t *args,
                          void *data)
{
    struct lua_plugin *lp = flux_plugin_aux_get (p, "lua.plugin");
    struct lua_plugref *ref = data;
    lua_State *L = global_L;
    if (!ref) {
        shell_log_error ("lua plugin: %s: no ref for topic %s", lp->name, topic);
        return 0;
    }
    lua_rawgeti (L, LUA_REGISTRYINDEX, ref->lua_ref);
    lua_pushstring (L, topic);
    if (lua_pcall (L, 1, LUA_MULTRET, 0) != 0) {
        shell_log_error ("lua plugin %s: %s", lp->name, lua_tostring (L, -1));
        return -1;
    }
    if (lua_gettop (L) > 0) {
        int success = lua_toboolean (L, 1);
        if (!success)
            return -1;
    }
    lua_settop (L, 0);
    return 0;
}

/*  Add a "handler" entry on stack to the current lua plugin being
 *   registered. A handler entry is a table with "topic" and "fn"
 *   entries pointing to the topic glob and callback function
 *   of the handler being added.
 */
static int l_plugin_add_handler (lua_State *L,
                                 struct lua_plugin *lp,
                                 flux_plugin_t *p)
{
    struct lua_plugref *ref = NULL;
    const char *topic = NULL;

    /*  Get handler table entries "topic" and "fn"
     */
    lua_getfield (L, -1, "topic");
    if (!lua_isstring (L, -1))
        return luaL_error (L, "Missing or invalid 'topic' in handler entry");
    topic = lua_tostring (L, -1);

    lua_getfield (L, -2, "fn");
    if (!lua_isfunction (L, -1))
        return luaL_error (L, "Missing or invalid 'fn' in handler entry");

    /*  Save callback function in registry to call later:
     *  (pops the function from the stack)
     */
    if (!(ref = lua_plugref_create (topic, luaL_ref (L, LUA_REGISTRYINDEX))))
        return luaL_error (L, "plugref_create: %s", strerror (errno));

    if (flux_plugin_add_handler (p, topic, lua_plugin_cb, ref) < 0) {
        lua_plugref_destroy (ref);
        return luaL_error (L, flux_plugin_strerror (p));
    }
    zlistx_add_end (lp->refs, ref);

    /*  Pop "topic" still left on stack */
    lua_pop (L, 1);
    return 0;
}

/*  Implementation of plugin.register Lua method.
 */
static int l_plugin_register (lua_State *L)
{
    int i;
    int t;
    const char *name;
    struct lua_plugin *lp = lua_plugin_create (current_file ());
    flux_plugin_t *p = flux_plugin_create ();

    if (!lp || !p)
        goto error;
    flux_plugin_aux_set (p, "lua.plugin", lp, (flux_free_f) lua_plugin_destroy);

    /*  Argument to plugin.register is a table with "name" and "handlers"
     *  Save table position on stack.
     */
    t = lua_gettop (L);
    if (!lua_istable (L, t))
        return luaL_error (L, "plugin.create requires table argument");

    /*  Get name or use current filename as name for "anonymoous" plugin
     */
    lua_getfield (L, t, "name");
    if (lua_isnoneornil (L, -1))
        name = current_file ();
    else
        name = lua_tostring (L, -1);
    lua_pop (L, 1);

    lp->name = strdup (name);
    flux_plugin_set_name (p, name);

    /*  Get handlers "array"
     */
    lua_getfield (L, t, "handlers");
    if (!lua_istable (L, -1))
        return luaL_error (L, "required handlers table missing");

    /*  Iterate handlers array (Lua index starts at 1), and add
     *   a plugin handler for each entry.
     */
    i = 1;
    lua_rawgeti (L, -1, i);
    while (!lua_isnil (L, -1)) {
        (void ) l_plugin_add_handler (L, lp, p);
        lua_pop (L, 1);
        lua_rawgeti (L, -1, ++i);
    }

    /*  If no handlers were specified, assume this was a mistake in
     *   the plugin.register() call and throw an error.
     */
    if (i == 1)
        return luaL_error (L, "plugin.register: handlers table exists "
                              "but has no entries. (not an array?)");

    /*  Finally, add plugin to shell plugin  stack
     */
    if (plugstack_push (rc_shell->plugstack, p) < 0)
        return luaL_error (L, "plugstack_push: %s", strerror (errno));

    return 0;
error:
    lua_plugin_destroy (lp);
    flux_plugin_destroy (p);
    return luaL_error (L, "plugin.create failed");
}

static int isa_pattern (const char *str)
{
    return (strchr (str, '*') || strchr (str, '?') || strchr (str, '['));
}

/*  Implementation of plugin.load Lua method.
 */
static int plugin_load (lua_State *L)
{
    const char *pattern;
    char *conf = NULL;
    int t = lua_gettop (L);
    int rc = -1;

    if (lua_isstring (L, t))
        pattern = lua_tostring (L, -1);
    else if (lua_istable (L, t)) {
        lua_getfield (L, t, "file");

        pattern = lua_tostring (L, -1);
        lua_getfield (L, t, "conf");
        if (lua_istable (L, -1))
            lua_value_to_json_string (L, -1, &conf);
    }
    else
        return luaL_error (L, "plugin.load: invalid argument");

    if ((rc = plugstack_load (rc_shell->plugstack, pattern, conf)) < 0)
        luaL_error (L, "plugin.load: %s: %s", pattern, strerror (errno));
    else if (rc == 0 && !isa_pattern (pattern))
        luaL_error (L, "plugin.load: %s: File not found", pattern);

    free (conf);
    return rc;
}

static int l_plugin_load (lua_State *L)
{
    plugin_load (L);
    return 0;
}

/*  Run a Lua file as a shell initrc script
 */
static int shell_run_rcfile (flux_shell_t *shell,
                             lua_State *L,
                             const char *rcfile)
{
    struct stat sb;

    if (!shell || !L || !rcfile)
        return -1;

    shell_trace ("trying to load %s", rcfile);

    if (stat (rcfile, &sb) < 0)
        return -1;
    file_stack_push (rcfile);

    /*  Compile rcfile onto stack
     */
    if (luaL_loadfile (L, rcfile) != 0) {
        shell_log_error ("%s: %s", rcfile, lua_tostring (L, -1));
        return -1;
    }
    if (lua_pcall (L, 0, 0, 0) != 0) {
        shell_log_error ("%s", lua_tostring (L, -1));
        return -1;
    }
    file_stack_pop ();
    lua_settop (L, 0);
    return 0;
}

/*  Implementation of source() method. Load a glob of shell initrc files.
 */
static int l_source_rcfiles (lua_State *L)
{
    size_t i;
    int rc;
    glob_t gl;
    const char *pattern = lua_tostring (L, -1);
    int glob_flags = 0;

#ifdef GLOB_TILDE_CHECK
    glob_flags |= GLOB_TILDE_CHECK;
#endif
    if ((rc = glob (pattern, glob_flags, NULL, &gl)) != 0) {
        globfree (&gl);
        if (rc == GLOB_NOMATCH) {
            if (!isa_pattern (pattern))
                return luaL_error (L, "source %s: No such file or directory",
                                      pattern);
            else
                return 0;
        }
        else if (rc == GLOB_NOSPACE)
            return luaL_error (L, "Out of memory");
        else if (rc == GLOB_ABORTED)
            return luaL_error (L, "glob: failed to read %s", pattern);
        else
            return luaL_error (L, "glob: unknown rc = %d", rc);
    }

    for (i = 0; i < gl.gl_pathc; i++) {
        rc = shell_run_rcfile (rc_shell, L, gl.gl_pathv[i]);
        if (rc < 0)
            return luaL_error (L, "source %s failed", gl.gl_pathv[i]);
    }
    globfree (&gl);
    return 0;
}

static int l_source_if_exists (lua_State *L)
{
    struct stat sb;
    const char *file = luaL_checkstring (L, -1);
    if (stat (file, &sb) < 0)
        return 0;
    if (shell_run_rcfile (rc_shell, L, file) < 0)
        return luaL_error (L, "source %s failed");
    return 0;
}

/*  shell.info implementation
 */
static int l_shell_info (lua_State *L)
{
    int rc = 1;
    char *json_str;
    if (flux_shell_get_info (rc_shell, &json_str) < 0) {
        return lua_pusherror (L, "flux_shell_get_info: %s", strerror (errno));
    }
    if (json_object_string_to_lua (L, json_str) < 0)
        rc = lua_pusherror (L, "json_string_to_lua: %s", strerror (errno));
    free (json_str);
    return rc;
}

/*  shell.options indexer
 */
static int l_shell_getopt (lua_State *L)
{
    int rc = 1;
    const char *key = lua_tostring (L, -1);
    char *json_str = NULL;

    rc = flux_shell_getopt (rc_shell, key, &json_str);
    if (rc < 0)
        rc = lua_pusherror (L, "flux_shell_getopt: %s", strerror (errno));
    else if (rc == 0) {
        rc = 1;
        lua_pushnil (L);
    }
    else if (json_object_string_to_lua (L, json_str) < 0)
        rc = lua_pusherror (L, "json_string_to_lua: %s", strerror (errno));
    free (json_str);
    return rc;
}

/*  shell.options newindex handler
 */
static int l_shell_setopt (lua_State *L)
{
    int rc;
    char *s = NULL;
    const char *key = luaL_checkstring (L, -2);
    if (!lua_isnoneornil (L, -1) && lua_value_to_json_string (L, -1, &s) < 0)
        return lua_pusherror (L, "setopt: error converting value to json");
    rc = flux_shell_setopt (rc_shell, key, s);
    free (s);
    return l_pushresult (L, rc);
}

static const struct luaL_Reg options_methods [] = {
    { "__index",    l_shell_getopt },
    { "__newindex", l_shell_setopt },
    { NULL,         NULL           },
};

static int l_shell_pushoptions (lua_State *L)
{
    lua_newtable (L);
    luaL_setfuncs (L, options_methods, 0);
    lua_pushvalue (L, -1);
    lua_setmetatable (L, -2);
    return 1;
}

/*  shell.getenv
 */
static int l_shell_getenv (lua_State *L)
{
    if (lua_gettop (L) == 0)
        return json_object_to_lua (L, rc_shell->info->jobspec->environment);
    else {
        const char *key = lua_tostring (L, -1);
        const char *val = flux_shell_getenv (rc_shell, key);
        if (val)
            lua_pushstring (L, val);
        else
            lua_pushnil (L);
    }
    return 1;
}

/*  shell.unsetenv
 */
static int l_shell_unsetenv (lua_State *L)
{
    int rc = flux_shell_unsetenv (rc_shell, lua_tostring (L, -1));
    return l_pushresult (L, rc);
}

/*  shell.setenv
 */
static int l_shell_setenv (lua_State *L)
{
    const char *name = lua_tostring (L, 1);
    const char *val = lua_tostring (L, 2);
    int overwrite = 1;
    if (lua_gettop (L) == 3)
        overwrite = lua_tointeger (L, 3);
    int rc = flux_shell_setenvf (rc_shell, overwrite, name, "%s", val);
    if (rc < 0)
        return lua_pusherror (L, "%s", strerror (errno));
    lua_pushboolean (L, 1);
    return 1;
}

/*  shell.rankinfo (shell_rank)
 */
static int l_shell_rankinfo (lua_State *L)
{
    int rc = 1;
    char *json_str = NULL;
    int shell_rank = -1;

    if (lua_isnumber (L, -1))
        shell_rank = lua_tointeger (L, -1);

    if (flux_shell_get_rank_info (rc_shell, shell_rank, &json_str) < 0)
        return lua_pusherror (L, "%s", strerror (errno));
    if (json_object_string_to_lua (L, json_str) < 0)
        rc = lua_pusherror (L, "json_object_to_lua: %s", strerror (errno));
    free (json_str);
    return rc;
}

static void get_lua_sourceinfo (lua_State *L,
                                lua_Debug *arp,
                                const char **filep,
                                int *linep)
{
    /*  Get file/line info using lua Debug interface:
     */

    /* Prevent memcheck,ASan from complaining about
     *  uninitialized memory:
     */
    memset (arp, 0, sizeof (*arp));

    *linep = -1;
    *filep = NULL;
    if (lua_getstack (L, 1, arp) && lua_getinfo (L, "Sl", arp)) {
        *linep = arp->currentline;
        *filep = arp->short_src;
    }
}

static int call_shell_log (int level, lua_State *L)
{
    lua_Debug ar;
    const char *file;
    int line = -1;
    const char *s = lua_tostring (L, 1);

    get_lua_sourceinfo (L, &ar, &file, &line);
    flux_shell_log (NULL, level, file, line, "%s", s);
    return 0;
}

/*  shell.log (msg)
 */
static int l_shell_log (lua_State *L)
{
    return call_shell_log (FLUX_SHELL_NOTICE, L);
}

/*  shell.debug (msg)
 */
static int l_shell_debug (lua_State *L)
{
    return call_shell_log (FLUX_SHELL_DEBUG, L);
}

/*  shell.log_error (msg)
 */
static int l_shell_log_error (lua_State *L)
{
    return call_shell_log (FLUX_SHELL_ERROR, L);
}

/*  shell.die (msg)
 */
static int l_shell_die (lua_State *L)
{
    lua_Debug ar;
    int line;
    const char *file;
    const char *s = lua_tostring (L, 1);

    get_lua_sourceinfo (L, &ar, &file, &line);
    flux_shell_fatal (NULL, file, line, 0, 1, "%s", s);
    return 0;
}

static int l_plugin_index (lua_State *L)
{
    const char *key = lua_tostring (L, 2);

    if (key == NULL)
        return luaL_error (L, "plugin: invalid key");

    if (streq (key, "load")) {
        lua_pushcfunction (L, l_plugin_load);
        return 1;
    }
   if (streq (key, "register")) {
        lua_pushcfunction (L, l_plugin_register);
        return 1;
    }
    else if (streq (key, "searchpath")) {
        lua_pushstring (L, plugstack_get_searchpath (rc_shell->plugstack));
        return 1;
    } else {
        lua_rawget (L, -2);
        return 1;
    }
    return 0;
}

static int l_plugin_newindex (lua_State *L)
{
    const char *key = lua_tostring (L, 2);
    if (streq (key, "searchpath")) {
        const char *path = lua_tostring (L, 3);
        plugstack_set_searchpath (rc_shell->plugstack, path);
        return 0;
    }
    return luaL_error (L, "invalid plugin method %s called", key);
}

static int l_shell_index (lua_State *L)
{
    const char *key = lua_tostring (L, 2);

    if (key == NULL)
        return luaL_error (L, "shell: invalid key");

    if (streq (key, "info"))
        return l_shell_info (L);
    else if (streq (key, "getenv")) {
        lua_pushcfunction (L, l_shell_getenv);
        return 1;
    }
    else if (streq (key, "setenv")) {
        lua_pushcfunction (L, l_shell_setenv);
        return 1;
    }
    else if (streq (key, "unsetenv")) {
        lua_pushcfunction (L, l_shell_unsetenv);
        return 1;
    }
    else if (streq (key, "get_rankinfo")) {
        lua_pushcfunction (L, l_shell_rankinfo);
        return 1;
    }
    else if (streq (key, "rankinfo"))
        return l_shell_rankinfo (L);
    else if (streq (key, "verbose")) {
        lua_pushboolean (L, rc_shell->verbose);
        return 1;
    }
    else if (streq (key, "log")) {
        lua_pushcfunction (L, l_shell_log);
        return 1;
    }
    else if (streq (key, "debug")) {
        lua_pushcfunction (L, l_shell_debug);
        return 1;
    }
    else if (streq (key, "log_error")) {
        lua_pushcfunction (L, l_shell_log_error);
        return 1;
    }
    else if (streq (key, "die")) {
        lua_pushcfunction (L, l_shell_die);
        return 1;
    }
    else {
        lua_rawget (L, -2);
        return 1;
    }
    lua_pushnil (L);
    return 1;
}

const char *shell_fields[] = {
    "info", "getenv", "setenv", "unsetenv", "rankinfo", "get_rankinfo", NULL
};

static int is_shell_method (const char *name)
{
    const char **sp = &shell_fields [0];
    while (*sp != NULL) {
        if (streq (name, *sp))
            return 1;
        sp++;
    }
    return 0;
}

static int l_shell_newindex (lua_State *L)
{
    if (lua_isstring (L, -2)) {
        const char *key;

        /*  Copy key to ensure coercion to string is reverisble: */
        lua_pushvalue (L, -2);
        key = lua_tostring (L, -1);

        if (is_shell_method (key)) {
            return luaL_error (L, "attempt to set read-only field shell.%s",
                                  key);
        }

        if (streq (key, "verbose")) {
            int level;
            /*  Handle Lua's baffling choice for lua_tonumber(true) == nil
             *  allows shell.verbose = 1 or true to work.
             */
            if (lua_isnumber (L, -2))
                level = lua_tonumber (L, -2);
            else if (lua_isboolean (L, -2))
                level = lua_toboolean (L, -2);
            else
                return luaL_error (L, "invalid assignment to shell.verbose");
            rc_shell->verbose = level;
            return 0;
        }
        lua_pop (L, 1);
    }
    lua_rawset (L, -3);
    return 0;
}

/*  task.info
 */
static int l_task_info (lua_State *L, flux_shell_task_t *task)
{
    int rc = 1;
    char *json_str;
    if (flux_shell_task_get_info (task, &json_str) < 0) {
        return lua_pusherror (L, "flux_shell_task_get_info: %s",
                                 strerror (errno));
    }
    if (json_object_string_to_lua (L, json_str) < 0)
        rc = lua_pusherror (L, "json_string_to_lua: %s", strerror (errno));
    free (json_str);
    return rc;
}

/*  task.getenv
 */
static int l_task_getenv (lua_State *L)
{
    flux_cmd_t *cmd = flux_shell_task_cmd (flux_shell_current_task (rc_shell));
    const char *key = lua_tostring (L, -1);
    const char *val = flux_cmd_getenv (cmd, key);
    lua_pushstring (L, val);
    return 1;
}

/*  task.unsetenv
 */
static int l_task_unsetenv (lua_State *L)
{
    flux_cmd_t *cmd = flux_shell_task_cmd (flux_shell_current_task (rc_shell));
    const char *key = lua_tostring (L, -1);
    flux_cmd_unsetenv (cmd, key);
    return 0;
}

/*  task.setenv
 */
static int l_task_setenv (lua_State *L)
{
    flux_cmd_t *cmd = flux_shell_task_cmd (flux_shell_current_task (rc_shell));
    const char *key = lua_tostring (L, 1);
    const char *val = lua_tostring (L, 2);
    int overwrite = 1;
    if (lua_gettop (L) == 3)
        overwrite = lua_tointeger (L, 3);
    if (flux_cmd_setenvf (cmd, overwrite, key, "%s", val) < 0)
        return lua_pusherror (L, "%s", strerror (errno));
    lua_pushboolean (L, 1);
    return 1;
}

static int l_task_index (lua_State *L)
{
    flux_shell_task_t *task = flux_shell_current_task (rc_shell);
    const char *key = lua_tostring (L, 2);

    if (!task)
        return luaL_error (L, "attempt to access task outside of task context");

    if (key == NULL)
        return lua_pusherror (L, "invalid key %s", key);
    else if (streq (key, "info")) {
        return l_task_info (L, task);
    }
    else if (streq (key, "getenv")) {
        lua_pushcfunction (L, l_task_getenv);
        return 1;
    }
    else if (streq (key, "setenv")) {
        lua_pushcfunction (L, l_task_setenv);
        return 1;
    }
    else if (streq (key, "unsetenv")) {
        lua_pushcfunction (L, l_task_unsetenv);
        return 1;
    }
    return 0;
}

static const struct luaL_Reg plugin_methods [] = {
    { "__index",    l_plugin_index       },
    { "__newindex", l_plugin_newindex    },
    { NULL,         NULL                 },
};

static const struct luaL_Reg shell_methods [] = {
    { "__index",    l_shell_index        },
    { "__newindex", l_shell_newindex     },
    { NULL,      NULL                    },
};

static const struct luaL_Reg task_methods [] = {
    { "__index", l_task_index },
    { NULL,      NULL         },
};

int shell_rc (flux_shell_t *shell, const char *rcfile)
{
    lua_State *L = NULL;
    char *copy = NULL;

    if (!shell || !rcfile)
        return -1;
    if (!(copy = strdup (rcfile)))
        return -1;
    if (!(global_L = luaL_newstate ()))
        return -1;
    if (!(file_stack = zlistx_new ()))
        return -1;

    L = global_L;
    luaL_openlibs (L);

    /*  Push "plugin" table and set metatable to itself.
     */
    lua_newtable (L);
    luaL_setfuncs (L, plugin_methods, 0);
    lua_pushvalue (L, -1);
    lua_setmetatable (L, -2);
    lua_setglobal (L, "plugin");

    /*  Push "shell" table and set metatable to itself.
     */
    lua_newtable (L);
    luaL_setfuncs (L, shell_methods, 0);
    lua_pushstring (L, dirname (copy));
    lua_setfield (L, -2, "rcpath");

    l_shell_pushoptions (L);
    lua_setfield (L, -2, "options");

    lua_pushvalue (L, -1);
    lua_setmetatable (L, -2);
    lua_setglobal (L, "shell");

    /*  Push "task" table and set metatable to itself.
     *  XXX: later the task object should only appear during "plugin"
     *   callbacks.
     */
    lua_newtable (L);
    luaL_setfuncs (L, task_methods, 0);
    lua_pushvalue (L, -1);
    lua_setmetatable (L, -2);
    lua_setglobal (L, "task");

    lua_pushcfunction (L, l_source_rcfiles);
    lua_setglobal (L, "source");

    lua_pushcfunction (L, l_source_if_exists);
    lua_setglobal (L, "source_if_exists");

    /* Save shell global */
    rc_shell = shell;
    free (copy);

    /* Load any flux.shell Lua library */
    lua_getglobal (L, "require");
    lua_pushstring (L, "flux.shell");
    if (lua_pcall (L, 1, LUA_MULTRET, 0) != 0) {
        shell_debug ("Error loading flux.shell module: %s",
                     lua_tostring (L, -1));
    }
    else
        shell_trace ("Successfully loaded flux.shell module");
    lua_settop (L, 0);
    return shell_run_rcfile (shell, L, rcfile);
}

int shell_rc_close (void)
{
    rc_shell = NULL;
    if (global_L)
        lua_close (global_L);
    global_L = NULL;

    /*  Destroy file stack
     */
    zlistx_destroy (&file_stack);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
