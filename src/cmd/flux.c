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
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <stdbool.h>
#include <sys/param.h>
#include <glob.h>
#include <assert.h>
#include <flux/core.h>
#include <flux/optparse.h>
#include <pwd.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/environment.h"

#include "cmdhelp.h"
#include "builtin.h"

void exec_subcommand (const char *searchpath, bool vopt, char *argv[]);
bool flux_is_installed (void);
void setup_path (struct environment *env, const char *argv0);
void setup_keydir (struct environment *env, int flags);
static void print_environment (struct environment *env);
static void register_builtin_subcommands (optparse_t *p);

static struct optparse_option opts[] = {
    { .name = "verbose",         .key = 'v', .has_arg = 0,
      .usage = "Be verbose about environment and command search",
    },
    OPTPARSE_TABLE_END
};

static const char *default_cmdhelp_pattern (optparse_t *p)
{
    int *flags = optparse_get_data (p, "conf_flags");
    return flux_conf_get ("cmdhelp_pattern", *flags);
}

void usage (optparse_t *p)
{
    char *help_pattern;
    const char *val = getenv ("FLUX_CMDHELP_PATTERN");
    const char *def = default_cmdhelp_pattern (p);

    if (asprintf (&help_pattern, "%s%s%s",
                  def ? def : "",
                  val ? ":" : "",
                  val ? val : "") < 0)
        log_err_exit ("failed to get command help list!");

    optparse_print_usage (p);
    fprintf (stderr, "\n");
    emit_command_help (help_pattern, stderr);
}

static optparse_t * setup_optparse_parse_args (int argc, char *argv[])
{
    optparse_err_t e;
    optparse_t *p = optparse_create ("flux");
    if (p == NULL)
        log_err_exit ("optparse_create");
    optparse_set (p, OPTPARSE_USAGE, "[OPTIONS] COMMAND ARGS");
    e = optparse_add_option_table (p, opts);
    if (e != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_option_table() failed");

    // Disable automatic `--help' in favor of our own usage() from above
    e = optparse_set (p, OPTPARSE_OPTION_CB, "help", NULL);
    if (e != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set() failed");

    // Don't print internal subcommands in --help (we print subcommands using
    //  emit_command_help() above.
    e = optparse_set (p, OPTPARSE_PRINT_SUBCMDS, 0);
    if (e != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set (OPTPARSE_PRINT_SUBCMDS");

    register_builtin_subcommands (p);

    if (optparse_parse_args (p, argc, argv) < 0)
        exit (1);

    return (p);
}


int main (int argc, char *argv[])
{
    bool vopt = false;
    struct environment *env;
    optparse_t *p;
    const char *searchpath, *s;
    const char *argv0 = argv[0];
    int flags = 0;
    int optindex;

    log_init ("flux");

    p = setup_optparse_parse_args (argc, argv);

    if (!flux_is_installed ())
        flags |= CONF_FLAG_INTREE;
    optparse_set_data (p, "conf_flags", &flags);

    if (optparse_hasopt (p, "help")) {
        usage (p); // N.B. accesses "conf_flags"
        exit (0);
    }
    optindex = optparse_option_index (p);
    if (argc - optindex == 0) {
        usage (p);
        exit (1);
    }
    vopt = optparse_hasopt (p, "verbose");

    /* prepare the environment 'env' that will be passed to subcommands.
     */
    env = environment_create ();

    /* Add PATH to env and prepend path to this executable if necessary.
     */
    setup_path (env, argv0);

    /* Prepend config values to env values.
     * Note special handling of lua ;; (default path).
     */
    environment_from_env (env, "LUA_CPATH", "", ';');
    environment_no_dedup_push_back (env, "LUA_CPATH", ";;");
    environment_push (env, "LUA_CPATH", flux_conf_get ("lua_cpath_add", flags));
    environment_push (env, "LUA_CPATH", getenv ("FLUX_LUA_CPATH_PREPEND"));

    environment_from_env (env, "LUA_PATH", "", ';');
    environment_no_dedup_push_back (env, "LUA_PATH", ";;");
    environment_push (env, "LUA_PATH", flux_conf_get ("lua_path_add", flags));
    environment_push (env, "LUA_PATH", getenv ("FLUX_LUA_PATH_PREPEND"));

    environment_from_env (env, "PYTHONPATH", "", ':');
    environment_push (env, "PYTHONPATH", flux_conf_get ("python_path", flags));
    environment_push (env, "PYTHONPATH", getenv ("FLUX_PYTHONPATH_PREPEND"));

    if ((s = getenv ("MANPATH")) && strlen (s) > 0) {
        environment_from_env (env, "MANPATH", ":", ':');
        environment_push (env, "MANPATH", flux_conf_get ("man_path", flags));
    } else { /* issue 745 */
        char *s = xasprintf ("%s:", flux_conf_get ("man_path", flags));
        environment_set (env, "MANPATH", s, 0);
        free (s);
        environment_set_separator (env, "MANPATH", ':');
    }

    environment_from_env (env, "FLUX_EXEC_PATH", "", ':');
    environment_push (env, "FLUX_EXEC_PATH",
                      flux_conf_get ("exec_path", flags));
    environment_push (env, "FLUX_EXEC_PATH", getenv ("FLUX_EXEC_PATH_PREPEND"));

    environment_from_env (env, "FLUX_CONNECTOR_PATH", "", ':');
    environment_push (env, "FLUX_CONNECTOR_PATH",
                      flux_conf_get ("connector_path", flags));
    environment_push (env, "FLUX_CONNECTOR_PATH",
                      getenv ("FLUX_CONNECTOR_PATH_PREPEND"));

    environment_from_env (env, "FLUX_MODULE_PATH", "", ':');
    environment_push (env, "FLUX_MODULE_PATH",
                      flux_conf_get ("module_path", flags));
    environment_push (env, "FLUX_MODULE_PATH",
                      getenv ("FLUX_MODULE_PATH_PREPEND"));

    /* Set FLUX_SEC_DIRECTORY, possibly to $HOME/.flux.
     */
    setup_keydir (env, flags);

    if (getenv ("FLUX_URI"))
        environment_from_env (env, "FLUX_URI", "", 0); /* pass-thru */

    environment_from_env (env, "FLUX_RC1_PATH",
                          flux_conf_get ("rc1_path", flags), 0);
    environment_from_env (env, "FLUX_RC3_PATH",
                          flux_conf_get ("rc3_path", flags), 0);
    environment_from_env (env, "FLUX_PMI_LIBRARY_PATH",
                          flux_conf_get ("pmi_library_path", flags), 0);
    environment_from_env (env, "FLUX_WRECK_LUA_PATTERN",
                          flux_conf_get ("wreck_lua_pattern", flags), 0);
    environment_from_env (env, "FLUX_WREXECD_PATH",
                          flux_conf_get ("wrexecd_path", flags), 0);

    environment_apply(env);
    optparse_set_data (p, "env", env);

    if (vopt)
        print_environment (env);
    if (optparse_get_subcommand (p, argv [optindex])) {
        if (optparse_run_subcommand (p, argc, argv) < 0)
            exit (1);
    } else {
        searchpath = environment_get (env, "FLUX_EXEC_PATH");
        if (vopt)
            printf ("sub-command search path: %s\n", searchpath);
        exec_subcommand (searchpath, vopt, argv + optindex);
    }

    environment_destroy (env);
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
            log_err_exit ("readlink (/proc/self/exe)");
        flux_exe_dir = strip_trailing_dot_libs (dirname (flux_exe_path));
        exe_path_valid = true;
    }
    return xstrdup (flux_exe_dir);
}

bool flux_is_installed (void)
{
    char *selfdir = dir_self ();
    bool ret = false;

    if (!strcmp (selfdir, X_BINDIR))
        ret = true;
    free (selfdir);
    return ret;
}

/*
 * If flux command was run with relative or absolute path, then
 *  prepend the directory for the flux executable to PATH. This
 *  ensures that in "flux [OPTIONS] [COMMAND] flux" the second
 *  flux executable is the same as the first. This is important
 *  for example with "flux start".
 */
void setup_path (struct environment *env, const char *argv0)
{
    char *selfdir;
    assert (argv0);

    /*  If argv[0] was explicitly "flux" then assume PATH is already set */
    if (strcmp (argv0, "flux") == 0)
        return;
    environment_from_env (env, "PATH", "/bin:/usr/bin", ':');
    selfdir = dir_self ();
    environment_push (env, "PATH", selfdir);
    free (selfdir);
}

void setup_keydir (struct environment *env, int flags)
{
    const char *dir = getenv ("FLUX_SEC_DIRECTORY");
    char *new_dir = NULL;

    if (!dir)
        dir = flux_conf_get ("keydir", flags);
    if (!dir) {
        struct passwd *pw = getpwuid (getuid ());
        if (!pw)
            log_msg_exit ("Who are you!?!");
        dir = new_dir = xasprintf ("%s/.flux", pw->pw_dir);
    }
    if (!dir)
        log_msg_exit ("Could not determine keydir");
    environment_set (env, "FLUX_SEC_DIRECTORY", dir, 0);
    if (new_dir)
        free (new_dir);
}

void exec_subcommand_dir (bool vopt, const char *dir, char *argv[],
        const char *prefix)
{
    char *path = xasprintf ("%s%s%s%s",
            dir ? dir : "",
            dir ? "/" : "",
            prefix ? prefix : "", argv[0]);
    if (vopt)
        log_msg ("trying to exec %s", path);
    execvp (path, argv); /* no return if successful */
    free (path);
}

void exec_subcommand (const char *searchpath, bool vopt, char *argv[])
{
    if (strchr (argv[0], '/')) {
        exec_subcommand_dir (vopt, NULL, argv, NULL);
        log_err_exit ("%s", argv[0]);
    } else {
        char *cpy = xstrdup (searchpath);
        char *dir, *saveptr = NULL, *a1 = cpy;

        while ((dir = strtok_r (a1, ":", &saveptr))) {
            exec_subcommand_dir (vopt, dir, argv, "flux-");
            a1 = NULL;
        }
        free (cpy);
        log_msg_exit ("`%s' is not a flux command.  See 'flux --help'", argv[0]);
    }
}

static void print_environment (struct environment *env)
{
    const char *val;
    for (val = environment_first (env); val; val = environment_next (env))
        printf("%s=%s\n", environment_cursor (env), val);
    fflush(stdout);
}

flux_t *builtin_get_flux_handle (optparse_t *p)
{
    flux_t *h = NULL;
    if ((h = optparse_get_data (p, "flux_t")))
        flux_incref (h);
    else if ((h = flux_open (NULL, 0)) == NULL)
        log_err_exit ("flux_open");
    return h;
}

static void register_builtin_subcommands (optparse_t *p)
{
    extern struct builtin_cmd builtin_cmds[];
    struct builtin_cmd *cmd = &builtin_cmds[0];
    while (cmd->reg_fn) {
        if (cmd->reg_fn (p) < 0)
            log_msg_exit ("register builtin %s failed\n", cmd->name);
        cmd++;
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
