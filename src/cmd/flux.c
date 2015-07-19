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
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <stdbool.h>
#include <sys/param.h>
#include <glob.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/setenvf.h"
#include "src/common/libutil/shortjson.h"


bool handle_internal (flux_conf_t cf, int ac, char *av[]);
void dump_environment (void);
void exec_subcommand (const char *searchpath, bool vopt, char *argv[]);
char *intree_confdir (void);

void setup_lua_env (flux_conf_t cf, const char *cpath_add, const char *path_add);
char *setup_exec_searchpath (flux_conf_t cf, const char *path_add);
void setup_module_env (flux_conf_t cf, const char *path_add);
void setup_python_env (flux_conf_t cf, const char *path_add);
void setup_connector_env (flux_conf_t cf, const char *path_add);
void setup_broker_env (flux_conf_t cf, const char *path_override);

#define OPTIONS "+T:tx:hM:O:B:vc:L:P:C:FS:u:"
static const struct option longopts[] = {
    {"tmpdir",          required_argument,  0, 'T'},
    {"trace-handle",    no_argument,        0, 't'},
    {"exec-path",       required_argument,  0, 'x'},
    {"module-path",     required_argument,  0, 'M'},
    {"connector-path",  required_argument,  0, 'O'},
    {"broker-path",     required_argument,  0, 'B'},
    {"lua-path",        required_argument,  0, 'L'},
    {"python-path",     required_argument,  0, 'P'},
    {"lua-cpath",       required_argument,  0, 'C'},
    {"config",          required_argument,  0, 'c'},
    {"secdir",          required_argument,  0, 'S'},
    {"uri",             required_argument,  0, 'u'},
    {"verbose",         no_argument,        0, 'v'},
    {"file-config",     no_argument,        0, 'F'},
    {"help",            no_argument,        0, 'h'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr,
"Usage: flux [OPTIONS] COMMAND ARGS\n"
"    -x,--exec-path PATH   prepend PATH to command search path\n"
"    -M,--module-path PATH prepend PATH to module search path\n"
"    -O,--connector-path PATH   prepend PATH to connector search path\n"
"    -L,--lua-path PATH    prepend PATH to LUA_PATH\n"
"    -P,--python-path PATH prepend PATH to PYTHONPATH\n"
"    -C,--lua-cpath PATH   prepend PATH to LUA_CPATH\n"
"    -T,--tmpdir PATH      set FLUX_TMPDIR\n"
"    -t,--trace-handle     set FLUX_HANDLE_TRACE=1 before executing COMMAND\n"
"    -B,--broker-path FILE override path to flux broker\n"
"    -c,--config DIR       set path to config directory\n"
"    -F,--file-config      force use of config file, even if FLUX_TMPDIR set\n"
"    -S,--secdir DIR       set the directory where CURVE keys will be stored\n"
"    -u,--uri URI          override default URI to flux broker\n"
"    -v,--verbose          show FLUX_* environment and command search\n"
"\n"
"The flux-core commands are:\n"
"   help          Display manual for a sub-command\n"
"   keygen        Generate CURVE keypairs for session security\n"
"   start         Bootstrap a comms session interactively\n"
"   kvs           Access the Flux the key-value store\n"
"   module        Load/unload comms modules\n"
"   up            Show state of all broker ranks\n"
"   ping          Time round-trip RPC on the comms rank-request network\n"
"   mping         Time round-trip group RPC to the mecho comms module\n"
"   snoop         Snoop on local Flux message broker traffic\n"
"   event         Publish and subscribe to Flux events\n"
"   logger        Log a message to Flux logging system\n"
"   comms         Misc Flux comms session operations\n"
"   comms-stats   Display comms message counters, etc.\n"
"   topo          Display current comms topology using graphviz\n"
"   wreckrun      Execute a Flux lightweight job (LWJ)\n"
"   zio           Manipulate KVS streams (including LWJ stdio)\n"
"   config        Manipulate a Flux config file\n"
);
}


int main (int argc, char *argv[])
{
    int ch;
    bool vopt = false;
    char *xopt = NULL;
    char *Mopt = NULL;
    char *Popt = NULL;
    char *Oopt = NULL;
    char *Bopt = NULL;
    char *Lopt = NULL;
    char *Copt = NULL;
    bool Fopt = false;
    char *confdir = NULL;
    char *secdir = NULL;
    flux_conf_t cf;
    char *searchpath;

    log_init ("flux");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'c': /* --config DIR */
                confdir = xstrdup (optarg);
                break;
            case 'S': /* --secdir DIR */
                secdir = optarg;
                break;
            case 'F': /* --file-config */
                Fopt = true;
                break;
            case 'T': /* --tmpdir PATH */
                if (setenv ("FLUX_TMPDIR", optarg, 1) < 0)
                    err_exit ("setenv");
                break;
            case 't': /* --trace-handle */
                if (setenv ("FLUX_HANDLE_TRACE", "1", 1) < 0)
                    err_exit ("setenv");
                break;
            case 'M': /* --module-path PATH */
                Mopt = optarg;
                break;
            case 'O': /* --connector-path PATH */
                Oopt = optarg;
                break;
            case 'x': /* --exec-path PATH */
                xopt = optarg;
                break;
            case 'B': /* --broker-path PATH */
                Bopt = optarg;
                break;
            case 'v': /* --verbose */
                vopt = true;
                break;
            case 'L': /* --lua-path PATH */
                Lopt = optarg;
                break;
            case 'P': /* --python-path PATH */
                Popt = optarg;
                break;
            case 'C': /* --lua-cpath PATH */
                Copt = optarg;
                break;
            case 'u': /* --uri URI */
                if (setenv ("FLUX_URI", optarg, 1) < 0)
                    err_exit ("setenv");
                break;
            case 'h': /* --help  */
                usage ();
                exit (0);
                break;
            default:
                usage ();
                exit (1);
        }
    }
    argc -= optind;
    argv += optind;

    cf = flux_conf_create ();
    /* Set the config directory and pass it to sub-commands.
    */
    if (confdir || (confdir = intree_confdir ()))
        flux_conf_set_directory (cf, confdir);
    if (setenv ("FLUX_CONF_DIRECTORY", flux_conf_get_directory (cf), 1) < 0)
        err_exit ("setenv");
    if (setenv ("FLUX_SEC_DIRECTORY",
                secdir ? secdir : flux_conf_get_directory (cf), 1) < 0)
        err_exit ("setenv");
    if (unsetenv ("FLUX_CONF_USEFILE") < 0)
        err_exit ("setenv");
    /* Process config from the KVS if not a bootstrap instance, and not
     * forced to use a config file by the command line.
     * It is not an error if config is not foud in either place, we will
     * try to make do with compiled-in defaults.
     */
    if (!Fopt && getenv ("FLUX_TMPDIR")
              && !(argc > 0 && !strcmp (argv[0], "start"))) {
        flux_t h;
        if (flux_conf_load (cf) == 0)
            setup_connector_env (cf, Oopt); /* flux_open() needs this */
        if (!(h = flux_open (NULL, 0)))     /*   esp. for in-tree */
            err_exit ("flux_open");
        if (kvs_conf_load (h, cf) < 0 && errno != ENOENT)
            err_exit ("could not load config from KVS");
        flux_close (h);
    } else {
        if (flux_conf_load (cf) == 0) {
            if (setenv ("FLUX_CONF_USEFILE", "1", 1) < 0)
                err_exit ("setenv");
        } else if (errno != ENOENT || Fopt)
            err_exit ("%s", flux_conf_get_directory (cf));
    }

    /* We share a few environment variables with sub-commands, so
     * that they don't have to reprocess the config.
     */
    setup_lua_env (cf, Copt, Lopt); /* sets LUA_CPATH, LUA_PATH */
    setup_module_env (cf, Mopt);    /* sets FLUX_MODULE_PATH */
    setup_python_env (cf, Popt);    /* sets PYTHONPATH */
    setup_connector_env (cf, Oopt); /* sets FLUX_CONNECTOR_PATH */
    setup_broker_env (cf, Bopt);    /* sets FLUX_BROKER_PATH */

    searchpath = setup_exec_searchpath (cf, xopt);

    if (argc == 0) {
        usage ();
        exit (1);
    }
    if (vopt)
        dump_environment ();
    if (!handle_internal (cf, argc, argv)) {
        if (vopt)
            printf ("sub-command search path: %s\n", searchpath);
        exec_subcommand (searchpath, vopt, argv);
    }

    if (confdir)
        free (confdir);

    free (searchpath);
    flux_conf_destroy (cf);
    log_fini ();

    return 0;
}

/*  Return directory containing this executable.  Caller must free.
 *   (using non-portable /proc/self/exe support for now)
 */
char *dir_self (void)
{
    char  flux_exe_path [MAXPATHLEN];
    char *flux_exe_dir;

    memset (flux_exe_path, 0, MAXPATHLEN);
    if (readlink ("/proc/self/exe", flux_exe_path, MAXPATHLEN - 1) < 0)
        err_exit ("readlink (/proc/self/exe)");
    flux_exe_dir = dirname (flux_exe_path);
    return xstrdup (flux_exe_dir);
}

char *intree_confdir (void)
{
    char *confdir = NULL;
    char *selfdir = dir_self ();

    if (strcmp (selfdir, X_BINDIR) != 0){
        confdir = xasprintf ("%s/%s../../etc/flux",
                selfdir,
                strstr(selfdir, "/.libs") != NULL ? "../" : "");
    }
    free (selfdir);
    return confdir;
}

static void path_push (char **path, const char *add, const char *sep)
{
    char *new;
    if (add == NULL || strlen(add) == 0) /* do nothing */
        return;
    new = xasprintf ("%s%s%s", add, *path ? sep : "",
            *path ? *path : "");
    free (*path);
    *path = new;
}

void setup_path(flux_conf_t cf,
        char ** base_path,
        const char * build_default,
        const char * config_name,
        const char * path_add,
        const char * sep
        )
{
    const char *cf_path = flux_conf_get (cf, config_name);

    path_push (base_path, build_default, sep);
    path_push (base_path, cf_path, sep);
    path_push (base_path, path_add, sep);
}

void setup_lua_env (flux_conf_t cf, const char *cpath_add, const char *path_add)
{
    char *path = NULL, *cpath = NULL;

    path_push (&path, getenv ("LUA_PATH"), ";");
    path_push (&cpath, getenv ("LUA_CPATH"), ";");

    if (path == NULL)
        path_push (&path, ";;", ";"); /* Lua replaces ;; with the default path */
    if (cpath == NULL)
        path_push (&cpath, ";;", ";");

    setup_path(cf, &path, LUA_PATH_ADD, "general.lua_path", path_add, ";");
    setup_path(cf, &cpath, LUA_CPATH_ADD, "general.lua_cpath", cpath_add, ";");

    if (setenv ("LUA_CPATH", cpath, 1) < 0)
        err_exit ("%s", cpath);
    if (setenv ("LUA_PATH", path, 1) < 0)
        err_exit ("%s", path);

    free (path);
    free (cpath);
}

char *setup_exec_searchpath (flux_conf_t cf, const char *path_add)
{
    char * path = NULL;
    setup_path(cf, &path, EXEC_PATH, "general.exec_path", path_add, ":");
    return path;
}

void setup_module_env (flux_conf_t cf, const char *path_add)
{
    char *path = NULL;
    setup_path(cf, &path, MODULE_PATH, "general.module_path", path_add, ":");

    if (setenv ("FLUX_MODULE_PATH", path, 1) < 0)
        err_exit ("%s", path);

    free (path);
}

void setup_python_env (flux_conf_t cf, const char *path_add)
{
    char *path = NULL;

    path_push (&path, getenv ("PYTHONPATH"), ":");

    setup_path(cf, &path, PYTHON_PATH, "general.python_path", path_add, ":");

    if (setenv ("PYTHONPATH", path, 1) < 0)
        err_exit ("%s", path);

    free (path);
}

void setup_connector_env (flux_conf_t cf, const char *path_add)
{
    char *path = NULL;
    setup_path(cf, &path, CONNECTOR_PATH, "general.connector_path", path_add, ":");

    if (setenv ("FLUX_CONNECTOR_PATH", path, 1) < 0)
        err_exit ("%s", path);

    free (path);
}

void setup_broker_env (flux_conf_t cf, const char *path_override)
{
    const char *cf_path = flux_conf_get (cf, "general.broker_path");
    const char *path = path_override;

    if (!path)
        path = cf_path;
    if (!path)
        path = BROKER_PATH;
    if (setenv ("FLUX_BROKER_PATH", path, 1) < 0)
        err_exit ("%s", path);
}

void dump_environment_one (const char *name)
{
    char *s = getenv (name);
    printf ("%20s%s%s\n", name, s ? "=" : "",
            s ? s : " is not set");
}

void dump_environment (void)
{
    dump_environment_one ("FLUX_MODULE_PATH");
    dump_environment_one ("FLUX_CONNECTOR_PATH");
    dump_environment_one ("FLUX_BROKER_PATH");
    dump_environment_one ("FLUX_TMPDIR");
    dump_environment_one ("FLUX_CONF_DIRECTORY");
    dump_environment_one ("FLUX_CONF_USEFILE");
    dump_environment_one ("FLUX_SEC_DIRECTORY");
    dump_environment_one ("FLUX_HANDLE_TRACE");
    dump_environment_one ("LUA_PATH");
    dump_environment_one ("LUA_CPATH");
    fflush(stdout);
}

void exec_subcommand_dir (bool vopt, const char *dir, char *argv[],
        const char *prefix)
{
    char *path = xasprintf ("%s%s%s%s",
            dir ? dir : "",
            dir ? "/" : "",
            prefix ? prefix : "", argv[0]);
    if (vopt)
        msg ("trying to exec %s", path);
    execvp (path, argv); /* no return if successful */
    free (path);
}

void exec_subcommand (const char *searchpath, bool vopt, char *argv[])
{
    if (strchr (argv[0], '/')) {
        exec_subcommand_dir (vopt, NULL, argv, NULL);
        err_exit ("%s", argv[0]);
    } else {
        char *cpy = xstrdup (searchpath);
        char *dir, *saveptr = NULL, *a1 = cpy;

        while ((dir = strtok_r (a1, ":", &saveptr))) {
            exec_subcommand_dir (vopt, dir, argv, "flux-");
            a1 = NULL;
        }
        free (cpy);
        msg_exit ("`%s' is not a flux command.  See 'flux --help'", argv[0]);
    }
}

void internal_help (flux_conf_t cf, const char *topic)
{
    const char *cf_path = flux_conf_get (cf, "general.man_path");
    char *cmd;

    if (topic) {
        if (cf_path)
            setenvf ("MANPATH", 1, "%s:%s", cf_path, MANDIR);
        else
            setenv ("MANPATH", MANDIR, 1);
        cmd = xasprintf ("man flux-%s %s", topic, topic);
        if (system (cmd) < 0)
            err_exit ("man");
        free (cmd);
    } else
        usage ();
}

bool handle_internal (flux_conf_t cf, int ac, char *av[])
{
    bool handled = true;

    if (!strcmp (av[0], "help")) {
        internal_help (cf, ac > 1 ? av[1] : NULL);
    } else
        handled = false;

    return handled;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
