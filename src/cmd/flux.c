/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <glob.h>
#include <assert.h>
#include <flux/core.h>
#include <flux/optparse.h>
#include <pwd.h>

#include "ccan/str/str.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/environment.h"
#include "src/common/libutil/intree.h"

#include "cmdhelp.h"
#include "builtin.h"

void exec_subcommand (const char *path, bool vopt, int argc, char *argv[]);
bool flux_is_installed (void);
void setup_path (struct environment *env, const char *argv0);
void setup_keydir (struct environment *env, int flags);
static void print_environment (struct environment *env);
static void register_builtin_subcommands (optparse_t *p);
static void push_parent_environment (optparse_t *p, struct environment *env);
static int current_instance_level (optparse_t *p);

static struct optparse_option opts[] = {
    { .name = "verbose",
      .key = 'v',
      .has_arg = 0,
      .usage = "Be verbose about environment and command search",
    },
    { .name = "version",
      .key = 'V',
      .has_arg = 0,
      .usage = "Display command and component versions",
    },
    { .name = "parent",
      .key = 'p',
      .has_arg = 0,
      .usage = "Set environment of parent instead of current instance",
    },
    { .name = "root",
      .key = 'r',
      .has_arg = 0,
      .usage = "Set environment of root instead of current instance",
    },
    OPTPARSE_TABLE_END
};

static const char *default_cmdhelp_pattern (optparse_t *p)
{
    int *flags = optparse_get_data (p, "conf_flags");
    return flux_conf_builtin_get ("cmdhelp_pattern", *flags);
}

void usage (optparse_t *p)
{
    char *help_pattern = NULL;
    const char *val = getenv ("FLUX_CMDHELP_PATTERN");
    const char *def = default_cmdhelp_pattern (p);

    if (asprintf (&help_pattern,
                  "%s%s%s",
                  def ? def : "",
                  val ? ":" : "",
                  val ? val : "") < 0)
        log_err_exit ("failed to get command help list!");

    optparse_print_usage (p);
    fprintf (stderr, "\n");
    fprintf (stderr, "For general Flux documentation, please visit\n");
    fprintf (stderr, "    https://flux-framework.readthedocs.io\n");
    emit_command_help (help_pattern, stderr);
    fprintf (stderr, "\n");
    fprintf (stderr, "See 'flux help COMMAND' for more information about ");
    fprintf (stderr, "a specific command.\n");
    free (help_pattern);
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
    int flags = FLUX_CONF_INSTALLED;
    int optindex;
    int n;

    log_init ("flux");

    p = setup_optparse_parse_args (argc, argv);

    if (!flux_is_installed ())
        flags = FLUX_CONF_INTREE;
    optparse_set_data (p, "conf_flags", &flags);

    if (optparse_hasopt (p, "help")) {
        usage (p); // N.B. accesses "conf_flags"
        exit (0);
    }
    if (optparse_hasopt (p, "version")) {
        execlp (argv0, "flux", "version", (char *) NULL);
        log_err_exit ("Failed to run flux-version");
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
    environment_push (env,
                      "LUA_CPATH",
                      flux_conf_builtin_get ("lua_cpath_add", flags));
    environment_push (env, "LUA_CPATH", getenv ("FLUX_LUA_CPATH_PREPEND"));

    environment_from_env (env, "LUA_PATH", "", ';');
    environment_no_dedup_push_back (env, "LUA_PATH", ";;");
    environment_push (env,
                      "LUA_PATH",
                      flux_conf_builtin_get ("lua_path_add", flags));
    environment_push (env, "LUA_PATH", getenv ("FLUX_LUA_PATH_PREPEND"));

    if ((s = getenv ("FLUX_PYTHONPATH_PREPEND"))) {
        environment_from_env (env, "PYTHONPATH", "", ':');
        environment_push (env, "PYTHONPATH", s);
    }
    if ((s = getenv ("MANPATH")) && strlen (s) > 0) {
        environment_from_env (env, "MANPATH", ":", ':');
        environment_push (env,
                          "MANPATH",
                          flux_conf_builtin_get ("man_path", flags));
    } else { /* issue 745 */
        char *s = xasprintf ("%s:", flux_conf_builtin_get ("man_path", flags));
        environment_set (env, "MANPATH", s, 0);
        free (s);
        environment_set_separator (env, "MANPATH", ':');
    }

    environment_from_env (env, "FLUX_EXEC_PATH", "", ':');
    environment_push (env,
                      "FLUX_EXEC_PATH",
                      flux_conf_builtin_get ("exec_path", flags));
    environment_push (env, "FLUX_EXEC_PATH", getenv ("FLUX_EXEC_PATH_PREPEND"));

    environment_from_env (env, "FLUX_CONNECTOR_PATH", "", ':');
    environment_push (env,
                      "FLUX_CONNECTOR_PATH",
                      flux_conf_builtin_get ("connector_path", flags));
    environment_push (env,
                      "FLUX_CONNECTOR_PATH",
                      getenv ("FLUX_CONNECTOR_PATH_PREPEND"));

    environment_from_env (env, "FLUX_MODULE_PATH", "", ':');
    environment_push (env,
                      "FLUX_MODULE_PATH",
                      flux_conf_builtin_get ("module_path", flags));
    environment_push (env,
                      "FLUX_MODULE_PATH",
                      getenv ("FLUX_MODULE_PATH_PREPEND"));

    if (getenv ("FLUX_URI"))
        environment_from_env (env, "FLUX_URI", "", 0); /* pass-thru */

    /*  Deduplicate any other FLUX_* PATH-type environment variables by
     *   calling environment_from_env() on them
     */
    environment_from_env (env, "FLUX_RC_EXTRA", NULL, ':');
    environment_from_env (env, "FLUX_SHELL_RC_PATH", NULL, ':');

    environment_apply (env);

    /* If --parent, push parent environment for each occurrence.
     * If --root, act as if --parent was used instance-level times.
     */
    if (optparse_hasopt (p, "root"))
        n = current_instance_level (p);
    else
        n = optparse_getopt (p, "parent", NULL);
    while (n-- > 0) {
        push_parent_environment (p, env);
        environment_apply (env);
    }
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
        exec_subcommand (searchpath, vopt, argc - optindex, argv + optindex);
    }

    environment_destroy (env);
    optparse_destroy (p);
    log_fini ();

    return 0;
}

bool flux_is_installed (void)
{
    int rc = executable_is_intree ();
    if (rc < 0)
        log_err_exit ("Failed to determine if flux is installed");
    return (rc == 0);
}

void ensure_self_first_in_path (struct environment *e, const char *selfdir)
{
    const char *entry = NULL;
    char realdir[PATH_MAX+1];
    char path[PATH_MAX+1];

    while ((entry = environment_var_next (e, "PATH", entry))) {
        /*  Attempt to canonicalize path, skipping eny elements that
         *  can't be resolved.
         */
        if (!(realpath (entry, realdir)))
            continue;
        /*  If this path matches "selfdir", then the current flux
         *  executable already appears first in path. Return.
         */
        if (streq (realdir, selfdir))
            return;
        /*  Otherwise, check for a flux in this path element, if it
         *  is present and executable, then the current flux is not
         *  first in path, insert selfdir before this element.
         */
        if (snprintf (path,
                      sizeof (path),
                      "%s/flux",
                      realpath (entry, realdir)) >= sizeof (path)
            || access (path, R_OK|X_OK) == 0) {
            if (environment_insert (e, "PATH", (char *) entry, selfdir) < 0)
                break;
            return;
        }
    }
    /* No flux(1) found in current PATH, we can insert selfdir at back */
    environment_push_back (e, "PATH", selfdir);
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
    const char *selfdir = NULL;
    assert (argv0);

    /*  If argv[0] was explicitly "flux" then assume PATH is already set */
    if (streq (argv0, "flux"))
        return;
    if ((selfdir = executable_selfdir ())) {
        environment_from_env (env, "PATH", "/bin:/usr/bin", ':');
        ensure_self_first_in_path (env, selfdir);
    }
    else
        log_msg_exit ("Unable to determine flux executable dir");
}

void builtin_env_add_pythonpath (struct environment *env)
{
    /* prepend to PYTHONPATH, which is no longer done by default:
     */
    environment_from_env (env, "PYTHONPATH", "", ':');
    environment_push (env,
                      "PYTHONPATH",
                      flux_conf_builtin_get ("python_path", FLUX_CONF_AUTO));
    environment_push (env, "PYTHONPATH", getenv ("FLUX_PYTHONPATH_PREPEND"));
}

static void setup_python_wrapper_environment (void)
{
    struct environment *env;
    const char *val;

    /* Set FLUX_PYTHONPATH_ORIG to the current PYTHONPATH, then
     * prepend the builtin python_path to PYTHONPATH so the the
     * python wrapper py-runner.py can find the correct Flux
     * bindings. The wrapper will then reset PYTHONPATH to
     * FLUX_PYTHONPATH_ORIG to avoid polluting the user environment.
     */
    if (!(env = environment_create ()))
        log_err_exit ("error creating environment");
    if ((val = getenv ("PYTHONPATH")))
        environment_set (env, "FLUX_PYTHONPATH_ORIG", val, ':');
    builtin_env_add_pythonpath (env);
    environment_apply (env);
    environment_destroy (env);
}

/* Check for a flux-<command>.py in dir and execute it under the configured
 * PYTHON_INTERPRETER if found.
 */
void exec_subcommand_py (bool vopt,
                         const char *dir,
                         int argc,
                         char *argv[],
	                     const char *prefix)
{
    char *path = xasprintf ("%s%s%s%s.py",
                            dir ? dir : "",
                            dir ? "/" : "",
                            prefix ? prefix : "", argv[0]);

    if (access (path, R_OK|X_OK) == 0) {
        const char *wrapper = flux_conf_builtin_get ("python_wrapper",
                                                     FLUX_CONF_AUTO);
        char *av [argc+3];
        av[0] = PYTHON_INTERPRETER;
        av[1] = (char *) wrapper;
        av[2] = path;
        for (int i = 3; i < argc+3; i++)
            av[i] = argv[i-2];
        if (vopt)
            log_msg ("trying to exec %s %s %s",
                     PYTHON_INTERPRETER,
                     wrapper,
                     path);

        setup_python_wrapper_environment ();

        execvp (PYTHON_INTERPRETER, av);
    }
    free (path);
}

void exec_subcommand_dir (bool vopt,
                          const char *dir,
                          char *argv[],
                          const char *prefix)
{
    char *path = xasprintf ("%s%s%s%s",
                            dir ? dir : "",
                            dir ? "/" : "",
                            prefix ? prefix : "",
                            argv[0]);
    if (vopt)
        log_msg ("trying to exec %s", path);
    execvp (path, argv); /* no return if successful */
    free (path);
}

void exec_subcommand (const char *searchpath, bool vopt, int argc, char *argv[])
{
    if (strchr (argv[0], '/')) {
        exec_subcommand_dir (vopt, NULL, argv, NULL);
        log_err_exit ("%s", argv[0]);
    } else {
        char *cpy = xstrdup (searchpath);
        char *dir, *saveptr = NULL, *a1 = cpy;

        while ((dir = strtok_r (a1, ":", &saveptr))) {
            /*  Try executing command as a python script `flux-<cmd>.py`,
             *  then fall back to execing flux-<cmd> directly.
             */
            exec_subcommand_py (vopt, dir, argc, argv, "flux-");
            exec_subcommand_dir (vopt, dir, argv, "flux-");
            a1 = NULL;
        }
        free (cpy);
        log_msg_exit ("`%s' is not a flux command.  See 'flux --help'",
                      argv[0]);
    }
}

static flux_t *flux_open_internal (optparse_t *p)
{
    flux_t *h = NULL;
    if ((h = optparse_get_data (p, "flux_t")))
        flux_incref (h);
    else if ((h = flux_open (NULL, 0)) == NULL)
        log_err_exit ("flux_open");
    return h;
}

static void flux_close_internal (optparse_t *p)
{
    flux_t *h = optparse_get_data (p, "flux_t");
    if (h) {
        flux_close (h);
        optparse_set_data (p, "flux_t", NULL);
    }
}

static int current_instance_level (optparse_t *p)
{
    const char *s;
    char *endptr;
    long int l;
    flux_t *h;

    if (!(h = flux_open_internal (p)))
        log_err_exit ("flux_open");

    if (!(s = flux_attr_get (h, "instance-level")))
        log_err_exit ("failed to get instance-level attribute");

    errno = 0;
    l = strtol (s, &endptr, 10);
    if (errno != 0 || *endptr != '\0')
        log_err_exit ("got invalid instance-level attribute: %s", s);

    return (int) l;
}

static void push_parent_environment (optparse_t *p, struct environment *env)
{
    const char *uri;
    const char *ns;
    flux_t *h = flux_open_internal (p);

    if (h == NULL)
        log_err_exit ("flux_open");

    /*  If parent-uri doesn't exist then we are at the root instance,
     *   just do nothing.
     */
    if (!(uri = flux_attr_get (h, "parent-uri")))
        return;

    environment_set (env, "FLUX_URI", uri, 0);

    /*  Before closing current instance handle, set FLUX_KVS_NAMESPACE
     *   if parent-kvs-namespace attr is set.
     */
    if ((ns = flux_attr_get (h, "parent-kvs-namespace")))
        environment_set (env, "FLUX_KVS_NAMESPACE", ns, 0);
    else
        environment_unset (env, "FLUX_KVS_NAMESPACE");

    /*  Now close current handle. Next call to `flux_open()` will
     *  have FLUX_URI set to parent after environment_apply() is called.
     */
    flux_close_internal (p);
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
    return flux_open_internal (p);
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
