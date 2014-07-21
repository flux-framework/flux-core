/* flux.c - Flux command front-end */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <stdbool.h>
#include <json/json.h>
#include <sys/param.h>

#include "cmb.h"
#include "log.h"
#include "util.h"
#include "zmsg.h"

static char  flux_exe_path [MAXPATHLEN];
static char *flux_exe_dir;

void exec_subcommand (const char *exec_path, char *argv[]);

#define OPTIONS "+s:tx:h"
static const struct option longopts[] = {
    {"socket-path",     required_argument,  0, 's'},
    {"trace-apisocket", no_argument,        0, 't'},
    {"exec-path",       required_argument,  0, 'x'},
    {"help",            no_argument,        0, 'h'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr, 
"Usage: flux [--socket-path PATH] [--exec-path PATH]\n"
"            [--trace-apisock] [--help] COMMAND ARGS\n"
);
}

static void help (void)
{
    usage ();
    fprintf (stderr, "\nThe most commonly used flux commands are:\n"
"   kvs        Get and put simple values in the Flux key-value store\n"
"   kvswatch   Watch values in the Flux key-value store\n"
"   kvsdir     List key-value pairs in the Flux key-value store\n"
"   kvstorture Torture-test the Flux key-value store\n"
"   ping       Time round-trip RPC to a Flux plugin\n"
"   mecho      Time round-trip group RPC to the mecho plugin\n"
"   stats      Obtain message counts from a Flux plugin\n"
"   barrier    Execute a Flux barrier\n"
"   snoop      Snoop on local Flux message broker traffic\n"
"   event      Send and receive Flux events\n"
"   logger     Log a message to Flux logging system\n"
"   log        Manipulate flux logs\n"
"   info       Display local rank, session size, and treeroot status\n"
);
}

int setup_lua_env (const char *exedir)
{
    char *s;

    /* XXX: For now Lua paths are set relative to path of the executable.
     *  Once we know where these things will be installed we can make these
     *  paths configurable on installation.
     */
    s = getenv ("LUA_CPATH");
    setenvf ("LUA_CPATH", 1, "%s/dlua/?.so;%s", exedir, s ? s : ";;");
    s = getenv ("LUA_PATH");
    setenvf ("LUA_PATH", 1, "%s/dlua/?.lua;%s", exedir, s ? s : ";;");
    return (0);
}

int main (int argc, char *argv[])
{
    int ch;
    char *exec_path;
    bool hopt = false;
    
    log_init ("flux-kvs");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 's': /* --socket-path PATH */
                if (setenv ("FLUX_API_PATH", optarg, 1) < 0)
                    err_exit ("setenv FLUX_API_PATH=%s", optarg);
                break;
            case 't': /* --trace-apisock */
                if (setenv ("FLUX_TRACE_APISOCK", "1", 1) < 0)
                    err_exit ("setenv FLUX_TRACE_APISOCK=1");
                break;
            case 'x': /* --exec-path */
                if (setenv ("FLUX_EXEC_PATH", optarg, 1) < 0)
                    err_exit ("setenv FLUX_EXEC_PATH=%s", optarg);
                break;
            case 'h': /* --help  */
                hopt = true;
                break;
            default:
                usage ();
                exit (1);
        }
    }
    argc -= optind;
    argv += optind;

    /*  Set global execpath to path to this executable.
     *   (using non-portable /proc/self/exe support for now)
     */
    memset (flux_exe_path, 0, MAXPATHLEN);
    if (readlink ("/proc/self/exe", flux_exe_path, MAXPATHLEN - 1) < 0)
        err_exit ("readlink (/proc/self/exe)");
    flux_exe_dir = dirname (flux_exe_path);

    setup_lua_env (flux_exe_dir);

    if (!(exec_path = getenv ("FLUX_EXEC_PATH"))) {
        exec_path = EXEC_PATH;
        if (setenv ("FLUX_EXEC_PATH", exec_path, 1) < 0)
            err_exit ("setenv FLUX_EXEC_PATH=%s", optarg);
    }
    if (hopt) {
        if (argc > 0) {
            char *av[] = { argv[0], "--help", NULL };
            exec_subcommand (exec_path, av);
        } else
            help ();
        exit (0);
    }
    if (argc == 0) {
        usage ();
        exit (1);
    }

    exec_subcommand (exec_path, argv);

    log_fini ();

    return 0;
}

void exec_subcommand_dir (const char *dir, char *argv[], const char *prefix)
{
    char *path;
    if (asprintf (&path, "%s/%s%s", dir, prefix ? prefix : "", argv[0]) < 0)
        oom ();
    execvp (path, argv); /* no return if successful */
    free (path);
}

void exec_subcommand (const char *searchpath, char *argv[])
{
    char *cpy = xstrdup (searchpath);
    char *dir, *saveptr = NULL, *a1 = cpy;

    while ((dir = strtok_r (a1, ":", &saveptr))) {
        exec_subcommand_dir (dir, argv, NULL);
        a1 = NULL;
    }
    /* Also try flux_exe_path with flux- prepended */
    exec_subcommand_dir (flux_exe_dir, argv, "flux-");
    free (cpy);
    msg_exit ("`%s' is not a flux command.  See 'flux --help'", argv[0]);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
