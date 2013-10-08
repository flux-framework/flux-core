/****************************************************************************
 *
 *  Lua bindings for Practical Environment for Parallel Experimentation.
 *
 ****************************************************************************/
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>
#include <fcntl.h>

#include "log_msg.h"
#include "pepe-lua.h"

/****************************************************************************
 *  Datatypes
 ****************************************************************************/

struct lua_script {
    char *path;
    lua_State *global_L;
    lua_State *L;
    int lua_ref;
};

struct pepe_lua {
    lua_State *          L;       /*  Global lua state  */

    int                  rank;
    int                  nprocs;
    char *               nodelist;

    struct lua_script *  script;  /*  lua script object */
};


/****************************************************************************
 *  Functions
 ****************************************************************************/

static void lua_script_destroy (struct lua_script *s)
{
    if (s == NULL)
        return;
    if (s->L && s->global_L) {
        luaL_unref (s->global_L, LUA_REGISTRYINDEX, s->lua_ref);
        /* Only call lua_close() on global/main lua state */
        s->L = s->global_L = NULL;
    }
    free (s->path);
    free (s);
}

static struct lua_script * lua_script_create (lua_State *L, const char *path)
{
    struct lua_script *script = malloc (sizeof (*script));

    if (script == NULL)
        return (NULL);

    memset (script, 0, sizeof (*script));

    script->path = strdup (path);
    if (!script->path) {
        lua_script_destroy (script);
        return NULL;
    }

    script->global_L = L;
    script->L = lua_newthread (L);
    script->lua_ref = luaL_ref (L, LUA_REGISTRYINDEX);

    /*
     *  Now we need to redefine the globals table for this script/thread.
     *   this will keep each script's globals in a private namespace,
     *   (including all the spank callback functions).
     *   To do this, we define a new table in the current thread's
     *   state, and give that table's metatable an __index field that
     *   points to the real globals table, then replace this threads
     *   globals table with the new (empty) table.
     *
     */

    /*  New globals table */
    lua_newtable (script->L);

    /*  metatable for table on top of stack */
    lua_newtable (script->L);

    /*
     *  Now set metatable->__index to point to the real globals
     *   table. This way Lua will check the root global table
     *   for any nonexistent items in the current thread's global
     *   table.
     */
    lua_pushstring (script->L, "__index");
    lua_pushvalue (script->L, LUA_GLOBALSINDEX);
    lua_settable (script->L, -3);

    /*  Now set metatable for the new globals table */
    lua_setmetatable (script->L, -2);

    /*  And finally replace the globals table with the (empty)  table
     *   now at top of the stack
     */
    lua_replace (script->L, LUA_GLOBALSINDEX);

    return (script);
}

char * pepe_script_find (pepe_lua_t l, const char *name, char *buf, size_t len)
{
    char *home;

    if (buf == NULL || name == NULL)
        return (NULL);

    if (name[0] == '/' || name[0] == '.') {
        /*  Return explicit path */
        return strncpy (buf, name, len);
    }

    /*  Search ~/.pepe/$name */
    if ((home = getenv ("HOME"))) {
        snprintf (buf, len, "%s/.pepe/%s", home, name);
        if (access (buf, R_OK) >= 0)
            return (buf);
    }
    return (NULL);
}

static int pepe_lua_state_init (struct pepe_lua *l)
{
    l->L = luaL_newstate ();
    if (l->L == NULL)
        return (-1);

    luaL_openlibs (l->L);
    return (0);
}

void pepe_lua_state_destroy (struct pepe_lua *l)
{
    if (l == NULL)
        return;
    if (l->script)
        lua_script_destroy (l->script);
    if (l->L)
        lua_close (l->L);
    free (l->nodelist);
    free (l);
}

int l_err (lua_State *L, const char *msg)
{
    lua_pushnil (L);
    lua_pushstring (L, msg);
    return (2);
}

int l_success (lua_State *L)
{
    lua_pushboolean (L, 1);
    return (1);
}

int l_setenv (lua_State *L)
{
    int overwrite;
    const char *var;
    const char *val;

    if (!lua_istable  (L, 1))
        return luaL_error (L, "setenv: arg 1 expected table got %s",
                luaL_typename (L, 1));
    var = luaL_checkstring (L, 2);
    val = luaL_checkstring (L, 3);
    overwrite = lua_tonumber (L, 4); /* default is 0 */

    log_debug ("setenv (%s=%s)\n", var, val);
    if (setenv (var, val, overwrite) < 0)
        return (l_err (L, strerror (errno)));

    return (l_success (L));
}

int l_unsetenv (lua_State *L)
{
    const char *var;
    if (!lua_istable  (L, 1))
        return luaL_error (L, "unsetenv: arg 1 expected table got %s",
                luaL_typename (L, 1));
    var = luaL_checkstring (L, 2);

    log_debug ("unsetenv (%s)\n", var);
    if (unsetenv (var) < 0)
        return (l_err (L, strerror (errno)));

    return (l_success (L));
}

static int vec_to_lua_table (lua_State *L, char **av)
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

int l_getenv (lua_State *L)
{
    extern char **environ;
    const char *val;

    if (!lua_istable  (L, 1))
        return luaL_error (L, "getenv: arg #1 expected table got %s",
                luaL_typename (L, 1));

    if (lua_isnone (L, 2))
        return (vec_to_lua_table (L, environ));

    if ((val = getenv (luaL_checkstring (L, 2))))
        lua_pushstring (L, val);
    else {
        lua_pushnil (L);
        lua_pushstring (L, "Not found");
    }

    return (1);
}

static char **lua_table_to_vec (lua_State *L, int index)
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

    count = 0;
    lua_pushnil (L);
    while (lua_next (L, index) != 0) {
        /*  'key' is at index -2 and 'value' is at index -1 */
        const char *var = lua_tostring (L, -2);
        const char *val = lua_tostring (L, -1);

        env[count] = malloc (strlen (var) + strlen (val) + 2);
        sprintf (env[count], "%s=%s", var, val);
        /*  pop 'value' and save key for next iteration */
        lua_pop (L, 1);
        count++;
    }
    lua_pop (L, 1);

    env[count] = NULL;

    return (env);
}

static int fd_close_on_exec (int fd)
{
    return (fcntl (fd++, F_SETFD, FD_CLOEXEC));
}


static void fd_closeall_on_exec (int fd)
{
    int fdlimit = sysconf (_SC_OPEN_MAX);
    while (fd < fdlimit)
        (void) fd_close_on_exec (fd++);
}


struct exec_info {
    pid_t pid;
    int childfd;
    int parentfd;
};

struct exec_info * exec_info_create (void)
{
    int fdpair[2];
    struct exec_info * e;

    if (pipe (fdpair) < 0) {
        log_err ("pipe: %s", strerror (errno));
        return NULL;
    }

    fd_close_on_exec(fdpair[0]);
    fd_close_on_exec(fdpair[1]);

    e = malloc (sizeof (*e));
    e->childfd = fdpair[1];
    e->parentfd = fdpair[0];
    e->pid = -1;

    return (e);

}

static void exec_info_destroy (struct exec_info *e)
{
    if (e == NULL)
        return;

    (void) close (e->parentfd);
    (void) close (e->childfd);
    e->pid = -1;
    free (e);
}

static struct exec_info * fork_child_with_exec_info ()
{
    struct exec_info *e;

    if (!(e = exec_info_create ()))
        return (NULL);

    if ((e->pid = fork()) < 0) {
        exec_info_destroy (e);
        return (NULL);
    }
    else if (e->pid == 0)
        close (e->parentfd);
    else
        close (e->childfd);
    return (e);
}

int exec_info_send_errno (struct exec_info *e)
{
    return (write (e->childfd, &errno, sizeof (errno)));
}

int exec_info_wait_for_child (struct exec_info *e)
{
    int err;
    int n;
    if ((n = read (e->parentfd, &err, sizeof (err))) < 0) {
        log_err ("read: %s", strerror (errno));
        return (-1);
    }
    else if (n == 0)
        return (0);
    else
        return (err);
}

static int io_devnull (void)
{
    int devnull = open ("/dev/null", O_RDWR);
    if (devnull < 0)
        return (-1);
    /*
     *  Dup appropriate fds onto child STDIN/STDOUT/STDERR
     */
    if (  (dup2 (devnull, STDIN_FILENO) < 0)
       || (dup2 (devnull, STDOUT_FILENO) < 0)
       || (dup2 (devnull, STDERR_FILENO) < 0))
            return (-1);
    return (0);
}

static int lua_get_closeio_flag (lua_State *L)
{
    int found;
    /* Get pepe table on top of stack */
    lua_getglobal (L, "pepe");
    lua_getfield (L, -1, "nocloseio");
    found = lua_tonumber (L, -1);
    lua_pop (L, 2);
    return (found ? 0 : 1);
}


static int l_execute (lua_State *L)
{
    char *cmd = strdup (luaL_checkstring (L, 1));
    int err;
    struct exec_info *e;
    int closeio = lua_get_closeio_flag (L);

    if (cmd == NULL)
        return luaL_error (L, "bad argument");

    if (!(e = fork_child_with_exec_info()))
        return l_err (L, "fork failed");

    if (e->pid == 0) {
        /* exec cmd */
        char * const args[] = { "/bin/sh", "-c", cmd, NULL };

        /*  Set close-on-exec flag for all file descriptors, but
         *   don't touch stdout, stderr, stdin -- these descriptors
         *   should not ever be closed at program startup.
         */
        if (closeio)
            io_devnull ();
        fd_closeall_on_exec (3);

        /*  Check for explicit environment array provided on cmdline
         */
        if (lua_istable (L, 2)) {
            char **env = lua_table_to_vec (L, 2);
            if (env == NULL)
                log_fatal (1, "Failed to read lua env array!\n");
            if (execve (args[0], args, env) < 0)
                exec_info_send_errno (e);
            exit (127);
        } /* NORETURN */

        /*  Otherwise just use current environment */
        if (execv (args[0], args) < 0)
            exec_info_send_errno (e);
        exit(127);
    }

    if ((err = exec_info_wait_for_child (e)))
        return l_err (L, strerror (err));

    return l_success (L);
}


static const struct luaL_Reg pepe_functions [] = {
    { "run",      l_execute },
    { "setenv",   l_setenv },
    { "unsetenv", l_unsetenv },
    { "getenv",   l_getenv },
    { NULL,       NULL },
};

struct pepe_lua * pepe_lua_state_create (int nprocs, int rank)
{
    struct pepe_lua *l = malloc (sizeof (*l));
    struct lua_State *L;

    if (l == NULL)
        return (NULL);

    memset (l, sizeof (*l), 0);

    if (pepe_lua_state_init (l) < 0) {
        pepe_lua_state_destroy (l);
        return (NULL);
    }
    l->nprocs = nprocs;
    l->rank = rank;

    L = l->L;

    lua_newtable (L);

    /*
     *  Register functions into table on top of stack
     */
    luaL_register (L, NULL, pepe_functions);

    lua_pushnumber (L, nprocs);
    lua_setfield (L, -2, "nprocs");

    lua_pushnumber (L, rank);
    lua_setfield (L, -2, "rank");

    if ((l->nodelist = strdup (getenv ("SLURM_JOB_NODELIST")))) {
        lua_pushstring (L, l->nodelist);
        lua_setfield (L, -2, "nodelist");
    }

    luaL_loadstring (L, "pepe.run(string.format(unpack({...})))");
    lua_setfield (L, -2, "runf");

    lua_setglobal (L, "pepe");

    return (l);

}

static void print_lua_script_error (struct lua_script *script)
{
    const char *s = basename (script->path);
    const char *err = lua_tostring (script->L, -1);
    log_err ("%s: %s\n", s, err);
}

int pepe_lua_script_execute (pepe_lua_t l, const char *name)
{
    char *path;
    char buf [4096];
    struct lua_script *s;

    if (!(path = pepe_script_find (l, name, buf, sizeof (buf)))) {
        errno = EEXIST;
        return (-1);
    }

    log_verbose ("Found config script at %s\n", path);

    if (!(s = lua_script_create (l->L, path))) {
        errno = EINVAL;
        return (-1);
    }

    /*
     *  Load the script:
     */
    if (luaL_loadfile (s->L, s->path) ||
        lua_pcall (s->L, 0, 0, 0)) {
        print_lua_script_error (s);
        return (-1);
    }

    return (0);
}


/* vi: ts=4 sw=4 expandtab
 */
