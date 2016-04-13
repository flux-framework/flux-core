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
#include <assert.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/optparse.h"

#include "cmdhelp.h"
#include "builtin.h"

void exec_subcommand (const char *searchpath, bool vopt, char *argv[]);
char *intree_confdir (void);
void setup_path (flux_conf_t cf, const char *argv0);
static void print_environment (flux_conf_t cf);
static void register_builtin_subcommands (optparse_t *p);

static struct optparse_option opts[] = {
    { .name = "trace-handle",    .key = 't', .has_arg = 0,
      .usage = "Set FLUX_HANDLE_TRACE=1 before executing COMMAND"
    },
    { .name = "config",          .key = 'c', .has_arg = 1,
      .arginfo = "DIR",
      .usage = "Set path to config directory"
    },
    { .name = "secdir",          .key = 'S', .has_arg = 1,
      .arginfo = "DIR",
      .usage = "Set the directory where CURVE keys will be stored"
    },
    { .name = "uri",             .key = 'u', .has_arg = 1,
      .arginfo = "URI",
      .usage = "Override default URI to flux broker"
    },
    { .name = "verbose",         .key = 'v', .has_arg = 0,
      .usage = "Be verbose about environment and command search",
    },
    OPTPARSE_TABLE_END
};

static char * get_help_pattern ()
{
    char *intree = intree_confdir ();
    char *pattern;
    if ((pattern = getenv ("FLUX_CMDHELP_PATTERN")))
        return strdup (pattern);
    if (intree == NULL)
        pattern = xasprintf ("%s/flux/help.d/*.json", X_DATADIR);
    else {
        pattern = xasprintf ("%s/help.d/*.json", intree);
        free (intree);
    }
    return (pattern);
}

void usage (optparse_t *p)
{
    char *help_pattern = get_help_pattern ();
    optparse_print_usage (p);
    fprintf (stderr, "\n");
    if (help_pattern) {
        emit_command_help (help_pattern, stderr);
        free (help_pattern);
    }
}

static optparse_t * setup_optparse_parse_args (int argc, char *argv[])
{
    optparse_err_t e;
    struct optparse_option helpopt = {
        .name = "help", .key = 'h', .usage = "Display this message"
    };
    optparse_t *p = optparse_create ("flux");
    if (p == NULL)
        err_exit ("optparse_create");
    optparse_set (p, OPTPARSE_USAGE, "[OPTIONS] COMMAND ARGS");
    e = optparse_add_option_table (p, opts);
    if (e != OPTPARSE_SUCCESS)
        msg_exit ("optparse_add_option_table() failed");

    // Remove automatic `--help' in favor of our own usage() from above
    e = optparse_remove_option (p, "help");
    if (e != OPTPARSE_SUCCESS)
        msg_exit ("optparse_remove_option (\"help\")");
    e = optparse_add_option (p, &helpopt);
    if (e != OPTPARSE_SUCCESS)
        msg_exit ("optparse_add_option (\"help\")");

    // Don't print internal subcommands in --help (we print subcommands using
    //  emit_command_help() above.
    e = optparse_set (p, OPTPARSE_PRINT_SUBCMDS, 0);
    if (e != OPTPARSE_SUCCESS)
        msg_exit ("optparse_set (OPTPARSE_PRINT_SUBCMDS");

    register_builtin_subcommands (p);

    if (optparse_parse_args (p, argc, argv) < 0)
        exit (1);

    return (p);
}


int main (int argc, char *argv[])
{
    const char *opt;
    bool vopt = false;
    char *confdir = NULL;
    const char *secdir = NULL;
    flux_conf_t cf;
    optparse_t *p;
    const char *searchpath;
    const char *argv0 = argv[0];

    log_init ("flux");

    cf = flux_conf_create ();

    p = setup_optparse_parse_args (argc, argv);

    optparse_set_data (p, "conf", cf);

    if (optparse_hasopt (p, "help")) {
        usage (p);
        exit (0);
    }
    vopt = optparse_hasopt (p, "verbose");
    if (optparse_hasopt (p, "trace-handle")) {
        if (setenv ("FLUX_HANDLE_TRACE", "1", 1) < 0)
            err_exit ("setenv");
        flux_conf_environment_set (cf, "FLUX_HANDLE_TRACE", "1", 0);
    }
    if (optparse_getopt (p, "uri", &opt)) {
        if (setenv ("FLUX_URI", opt, 1) < 0)
            err_exit ("setenv");
        flux_conf_environment_set(cf, "FLUX_URI", opt, 0);
    }

    optind = optparse_optind (p);

    /* Set the config directory and pass it to sub-commands.
    */
    if (optparse_getopt (p, "config", &opt))
        confdir = xstrdup (opt);
    if (confdir || (confdir = intree_confdir ()))
        flux_conf_set_directory (cf, confdir);


    /* Process configuration.
     * If not found, use compiled-in defaults.
     */
    if (flux_conf_load (cf) < 0 && (errno != ENOENT))
        err_exit ("%s", flux_conf_get_directory (cf));

    /*
     *  command line options that override environment and config:
     */
    if (!optparse_getopt (p, "secdir", &secdir))
        secdir = flux_conf_get_directory (cf);
    flux_conf_environment_set (cf, "FLUX_SEC_DIRECTORY", secdir, 0);

    /* Add PATH to flux_conf_environment and prepend path to
     *  this executable if necessary.
     */
    setup_path (cf, argv0);

    /* Add config items to environment variables */
    /* NOTE: I would prefer that this be in config, but kvs_load loads
     * everything out of band, preventing that */
    flux_conf_environment_push (cf, "FLUX_CONNECTOR_PATH", flux_conf_get(cf, "general.connector_path"));
    flux_conf_environment_push (cf, "FLUX_EXEC_PATH",      flux_conf_get(cf, "general.exec_path"));
    flux_conf_environment_push (cf, "FLUX_MODULE_PATH",    flux_conf_get(cf, "general.module_path"));
    flux_conf_environment_push (cf, "LUA_CPATH",           flux_conf_get(cf, "general.lua_cpath"));
    flux_conf_environment_push (cf, "LUA_PATH",            flux_conf_get(cf, "general.lua_path"));
    flux_conf_environment_push (cf, "PYTHONPATH",          flux_conf_get(cf, "general.python_path"));

    /* Prepend if FLUX_*_PATH_PREPEND is set */
    flux_conf_environment_push (cf, "FLUX_CONNECTOR_PATH",
                                getenv ("FLUX_CONNECTOR_PATH_PREPEND"));
    flux_conf_environment_push (cf, "FLUX_EXEC_PATH",
                                getenv ("FLUX_EXEC_PATH_PREPEND"));
    flux_conf_environment_push (cf, "FLUX_MODULE_PATH",
                                getenv ("FLUX_MODULE_PATH_PREPEND"));
    flux_conf_environment_push (cf, "LUA_CPATH",
                                getenv ("FLUX_LUA_CPATH_PREPEND"));
    flux_conf_environment_push (cf, "LUA_PATH",
                                getenv ("FLUX_LUA_PATH_PREPEND"));
    flux_conf_environment_push (cf, "PYTHONPATH",
                                getenv ("FLUX_PYTHONPATH_PREPEND"));

    if (argc - optind == 0) {
        usage (p);
        exit (1);
    }

    flux_conf_environment_apply(cf);

    if (vopt)
        print_environment (cf);
    if (optparse_get_subcommand (p, argv [optind])) {
        if (optparse_run_subcommand (p, argc, argv) < 0)
            exit (1);
    } else {
        searchpath = flux_conf_environment_get(cf, "FLUX_EXEC_PATH");
        if (vopt)
            printf ("sub-command search path: %s\n", searchpath);
        exec_subcommand (searchpath, vopt, argv + optind);
    }

    free (confdir);
    flux_conf_destroy (cf);
    optparse_destroy (p);
    log_fini ();

    return 0;
}

/*  Strip trailing ".libs", otherwise do nothing
 */
char *strip_trailing_dot_libs (char *dir)
{
    char *p = dir + strlen (dir) - 1;
    if (   (*(p--) == 's')
        && (*(p--) == 'b')
        && (*(p--) == 'i')
        && (*(p--) == 'l')
        && (*(p--) == '.')
        && (*p == '/') )
        *p = '\0';
    return (dir);
}

/*  Return directory containing this executable.  Caller must free.
 *   (using non-portable /proc/self/exe support for now)
 *   NOTE: build tree .libs directory stripped from path if found.
 */
char *dir_self (void)
{
    static char  flux_exe_path [MAXPATHLEN];
    static char *flux_exe_dir;
    static bool exe_path_valid = false;
    if (!exe_path_valid) {
        memset (flux_exe_path, 0, MAXPATHLEN);
        if (readlink ("/proc/self/exe", flux_exe_path, MAXPATHLEN - 1) < 0)
            err_exit ("readlink (/proc/self/exe)");
        flux_exe_dir = strip_trailing_dot_libs (dirname (flux_exe_path));
        exe_path_valid = true;
    }
    return xstrdup (flux_exe_dir);
}

char *intree_confdir (void)
{
    char *confdir = NULL;
    char *selfdir = dir_self ();

    if (strcmp (selfdir, X_BINDIR) != 0){
        confdir = xasprintf ("%s/../../etc/flux", selfdir);
    }
    free (selfdir);
    return confdir;
}

/*
 * If flux command was run with relative or absolute path, then
 *  prepend the directory for the flux executable to PATH. This
 *  ensures that in "flux [OPTIONS] [COMMAND] flux" the second
 *  flux executable is the same as the first. This is important
 *  for example with "flux start".
 */
void setup_path (flux_conf_t cf, const char *argv0)
{
    char *selfdir;
    assert (argv0);

    /*  If argv[0] was explicitly "flux" then assume PATH is already set */
    if (strcmp (argv0, "flux") == 0)
        return;
    flux_conf_environment_from_env (cf, "PATH", "/bin:/usr/bin", ':');
    selfdir = dir_self ();
    flux_conf_environment_push (cf, "PATH", selfdir);
    free (selfdir);
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

static void print_environment (flux_conf_t cf)
{
    const char *key, *value;
    for (value = (char*)flux_conf_environment_first(cf), key = (char*)flux_conf_environment_cursor(cf);
            value != NULL;
            value = flux_conf_environment_next(cf), key = flux_conf_environment_cursor(cf)) {
        printf("%s=%s\n", key, value);
    }
    fflush(stdout);
}

flux_t builtin_get_flux_handle (optparse_t *p)
{
    flux_t h = NULL;
    if ((h = optparse_get_data (p, "flux_t")))
        flux_incref (h);
    else if ((h = flux_open (NULL, 0)) == NULL)
        err_exit ("flux_open");
    return h;
}

static void register_builtin_subcommands (optparse_t *p)
{
    extern struct builtin_cmd builtin_cmds[];
    struct builtin_cmd *cmd = &builtin_cmds[0];
    while (cmd->reg_fn) {
        if (cmd->reg_fn (p) < 0)
            msg_exit ("register builtin %s failed\n", cmd->name);
        cmd++;
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
