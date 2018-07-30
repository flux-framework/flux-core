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
#include <sys/time.h>
#include <sys/resource.h>
#include <libgen.h>
#include <argz.h>
#include <flux/core.h>
#include <flux/optparse.h>
#include <czmq.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/setenvf.h"
#include "src/common/libpmi/simple_server.h"
#include "src/common/libpmi/dgetline.h"
#include "src/common/libsubprocess/subprocess.h"

#define DEFAULT_KILLER_TIMEOUT 2.0

static struct {
    double killer_timeout;
    flux_reactor_t *reactor;
    flux_watcher_t *timer;
    struct subprocess_manager *sm;
    optparse_t *opts;
    int size;
    int count;
    int exit_rc;
    struct {
        zhash_t *kvs;
        struct pmi_simple_server *srv;
    } pmi;
} ctx;

struct client {
    int rank;
    int fd;
    struct subprocess *p;
    flux_watcher_t *w;
    char buf[SIMPLE_MAX_PROTO_LINE];
};

void killer (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg);
int start_session (const char *cmd_argz, size_t cmd_argz_len,
                   const char *broker_path);
int exec_broker (const char *cmd_argz, size_t cmd_argz_len,
                 const char *broker_path);
char *create_scratch_dir (const char *session_id);
struct client *client_create (const char *broker_path, const char *scratch_dir,
                              int rank, const char *cmd_argz, size_t cmd_argz_len);
void client_destroy (struct client *cli);
char *find_broker (const char *searchpath);
static void setup_profiling_env (void);

#ifndef HAVE_CALIPER
static int no_caliper_fatal_err (optparse_t *p, struct optparse_option *o,
                                 const char *optarg)
{
    log_msg_exit ("Error: --caliper-profile used but no Caliper support found");
}
#endif /* !HAVE_CALIPER */

const char *usage_msg = "[OPTIONS] command ...";
static struct optparse_option opts[] = {
    { .name = "verbose",    .key = 'v', .has_arg = 0,
      .usage = "Be annoyingly informative", },
    { .name = "noexec",     .key = 'X', .has_arg = 0,
      .usage = "Don't execute (useful with -v, --verbose)", },
    { .name = "bootstrap",  .key = 'b', .has_arg = 1, .arginfo = "METHOD",
      .usage = "Set flux instance's network bootstrap method", },
    { .name = "size",       .key = 's', .has_arg = 1, .arginfo = "N",
      .usage = "Set number of ranks in new instance", },
    { .name = "broker-opts",.key = 'o', .has_arg = 1, .arginfo = "OPTS",
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "Add comma-separated broker options, e.g. \"-o,-v\"", },
    { .name = "killer-timeout",.key = 'k', .has_arg = 1, .arginfo = "SECONDS",
      .usage = "After a broker exits, kill other brokers after SECONDS", },
    { .name = "trace-pmi-server", .has_arg = 0, .arginfo = NULL,
      .usage = "Trace pmi simple server protocol exchange", },
    { .name = "scratchdir", .key = 'D', .has_arg = 1, .arginfo = "DIR",
      .usage = "Use DIR as scratch directory", },

/* Option group 1, these options will be listed after those above */
    { .group = 1,
      .name = "caliper-profile", .has_arg = 1,
      .arginfo = "PROFILE",
      .usage = "Enable profiling in brokers using Caliper configuration "
               "profile named `PROFILE'",
#ifndef HAVE_CALIPER
      .cb = no_caliper_fatal_err, /* Emit fatal err if not built w/ Caliper */
#endif /* !HAVE_CALIPER */
    },
    { .group = 1,
      .name = "wrap", .has_arg = 1, .arginfo = "ARGS,...",
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "Wrap broker execution in comma-separated arguments"
    },
    OPTPARSE_TABLE_END,
};

enum {
    BOOTSTRAP_PMI,
    BOOTSTRAP_SELFPMI
};

static struct {
    char *string;
    int num;
} bootstrap_options[] = {
    {"pmi", BOOTSTRAP_PMI},
    {"selfpmi", BOOTSTRAP_SELFPMI},
    {NULL, -1}
};

/* Turn the bootstrap option string into an integer value */
static int parse_bootstrap_option (optparse_t *opts)
{
    const char *bootstrap;
    int i;

    bootstrap = optparse_get_str (opts, "bootstrap", "pmi");
    for (i = 0; ; i++) {
        if (bootstrap_options[i].string == NULL)
            break;
        if (!strcmp(bootstrap_options[i].string, bootstrap))
            return bootstrap_options[i].num;
    }
    log_msg_exit("Unknown bootstrap method \"%s\"", bootstrap);
}

int main (int argc, char *argv[])
{
    int e, status = 0;
    char *command = NULL;
    size_t len = 0;
    const char *searchpath;
    int optindex;
    char *broker_path;
    int bootstrap;

    log_init ("flux-start");

    ctx.opts = optparse_create ("flux-start");
    if (optparse_add_option_table (ctx.opts, opts) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_option_table");
    if (optparse_set (ctx.opts, OPTPARSE_USAGE, usage_msg) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set usage");
    if ((optindex = optparse_parse_args (ctx.opts, argc, argv)) < 0)
        exit (1);
    ctx.killer_timeout = optparse_get_double (ctx.opts, "killer-timeout",
                                              DEFAULT_KILLER_TIMEOUT);
    if (ctx.killer_timeout < 0.)
        log_msg_exit ("--killer-timeout argument must be >= 0");
    if (optindex < argc) {
        if ((e = argz_create (argv + optindex, &command, &len)) != 0)
            log_errn_exit (e, "argz_create");
    }

    if (!(searchpath = getenv ("FLUX_EXEC_PATH")))
        log_msg_exit ("FLUX_EXEC_PATH is not set");
    if (!(broker_path = find_broker (searchpath)))
        log_msg_exit ("Could not locate broker in %s", searchpath);

    bootstrap = parse_bootstrap_option (ctx.opts);
    if (optparse_hasopt (ctx.opts, "size")) {
        if (bootstrap != BOOTSTRAP_SELFPMI) {
            if (!optparse_hasopt (ctx.opts, "bootstrap")) {
                bootstrap = BOOTSTRAP_SELFPMI;
                log_msg("warning: setting --bootstrap=selfpmi due to --size option");
            } else {
                log_errn_exit(EINVAL, "--size can only be used with --bootstrap=selfpmi");
            }
        }
        ctx.size = optparse_get_int (ctx.opts, "size", -1);
        if (ctx.size <= 0)
            log_msg_exit ("--size argument must be > 0");
    }

    setup_profiling_env ();

    switch (bootstrap) {
    case BOOTSTRAP_PMI:
        if (optparse_hasopt (ctx.opts, "scratchdir"))
            log_msg_exit ("--scratchdir only works with --bootstrap=selfpmi");
        status = exec_broker (command, len, broker_path);
        break;
    case BOOTSTRAP_SELFPMI:
        if (!optparse_hasopt (ctx.opts, "size"))
            log_msg_exit ("--size must be specified for --bootstrap=selfpmi");
        status = start_session (command, len, broker_path);
        break;
    default:
        assert(0); /* should never happen */
    }

    optparse_destroy (ctx.opts);
    free (broker_path);

    if (command)
        free (command);

    log_fini ();

    return status;
}

static void setup_profiling_env (void)
{
#if HAVE_CALIPER
    const char *profile;
    /*
     *  If --profile was used, set or append libcaliper.so in LD_PRELOAD
     *   to subprocess environment, swapping stub symbols for the actual
     *   libcaliper symbols.
     */
    if (optparse_getopt (ctx.opts, "caliper-profile", &profile) == 1) {
        const char *pl = getenv ("LD_PRELOAD");
        int rc = setenvf ("LD_PRELOAD", 1, "%s%s%s",
                          pl ? pl : "",
                          pl ? " ": "",
                          "libcaliper.so");
        if (rc < 0)
            log_err_exit ("Unable to set LD_PRELOAD in environment");

        if ((profile != NULL) &&
            (setenv ("CALI_CONFIG_PROFILE", profile, 1) < 0))
                log_err_exit ("setenv (CALI_CONFIG_PROFILE)");
        setenv ("CALI_LOG_VERBOSITY", "0", 0);
    }
#endif
}



char *find_broker (const char *searchpath)
{
    char *cpy = xstrdup (searchpath);
    char *dir, *saveptr = NULL, *a1 = cpy;
    char path[PATH_MAX];

    while ((dir = strtok_r (a1, ":", &saveptr))) {
        snprintf (path, sizeof (path), "%s/flux-broker", dir);
        if (access (path, X_OK) == 0)
            break;
        a1 = NULL;
    }
    free (cpy);
    return dir ? xstrdup (path) : NULL;
}

void killer (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct subprocess *p;

    p = subprocess_manager_first (ctx.sm);
    while (p) {
        if (subprocess_pid (p))
            (void)subprocess_kill (p, SIGKILL);
        p = subprocess_manager_next (ctx.sm);
    }
}

static int child_report (struct subprocess *p)
{
    struct client *cli = subprocess_get_context (p, "cli");
    pid_t pid = subprocess_pid (p);
    int sig;

    if ((sig = subprocess_stopped (p)))
        log_msg ("%d (pid %d) %s", cli->rank, pid, strsignal (sig));
    else if ((subprocess_continued (p)))
        log_msg ("%d (pid %d) %s", cli->rank, pid, strsignal (SIGCONT));
    else if ((sig = subprocess_signaled (p)))
        log_msg ("%d (pid %d) %s", cli->rank, pid, strsignal (sig));
    else if (subprocess_exited (p)) {
        int rc = subprocess_exit_code (p);
        if (rc >= 128)
            log_msg ("%d (pid %d) exited with rc=%d (%s)", cli->rank, pid, rc,
                                                       strsignal (rc - 128));
        else if (rc > 0)
            log_msg ("%d (pid %d) exited with rc=%d", cli->rank, pid, rc);
    } else
        log_msg ("%d (pid %d) status=%d", cli->rank, pid,
                                      subprocess_exit_status (p));
    return 0;
}

static int child_exit (struct subprocess *p)
{
    struct client *cli = subprocess_get_context (p, "cli");
    int rc = subprocess_exit_code (p);

    if (ctx.exit_rc < rc)
        ctx.exit_rc = rc;
    if (--ctx.count > 0)
        flux_watcher_start (ctx.timer);
    else
        flux_watcher_stop (ctx.timer);
    client_destroy (cli);
    return 0;
}

void add_args_list (char **argz, size_t *argz_len, optparse_t *opt, const char *name)
{
    const char *arg;
    optparse_getopt_iterator_reset (opt, name);
    while ((arg = optparse_getopt_next (opt, name)))
        if (argz_add  (argz, argz_len, arg) != 0)
            log_err_exit ("subprocess_argv_append");
}

char *create_scratch_dir (const char *session_id)
{
    char *tmpdir = getenv ("TMPDIR");
    char *scratchdir = xasprintf ("%s/flux-%s-XXXXXX",
                                  tmpdir ? tmpdir : "/tmp", session_id);

    if (!mkdtemp (scratchdir))
        log_err_exit ("mkdtemp %s", scratchdir);
    cleanup_push_string (cleanup_directory, scratchdir);
    return scratchdir;
}

static int pmi_response_send (void *client, const char *buf)
{
    struct client *cli = client;
    return dputline (cli->fd, buf);
}

static void pmi_debug_trace (void *client, const char *buf)
{
    struct client *cli = client;
    fprintf (stderr, "%d: %s", cli->rank, buf);
}

void pmi_simple_cb (flux_reactor_t *r, flux_watcher_t *w,
                    int revents, void *arg)
{
    struct client *cli = arg;
    int rc;
    if (dgetline (cli->fd, cli->buf, sizeof (cli->buf)) < 0)
        log_err_exit ("%s", __FUNCTION__);
    rc = pmi_simple_server_request (ctx.pmi.srv, cli->buf, cli);
    if (rc < 0)
        log_err_exit ("%s", __FUNCTION__);
    if (rc == 1) {
        close (cli->fd);
        cli->fd = -1;
        flux_watcher_stop (w);
    }
}

int pmi_kvs_put (void *arg, const char *kvsname,
                 const char *key, const char *val)
{
    zhash_update (ctx.pmi.kvs, key, xstrdup (val));
    zhash_freefn (ctx.pmi.kvs, key, (zhash_free_fn *)free);
    return 0;
}

int pmi_kvs_get (void *arg, void *client, const char *kvsname,
                 const char *key)
{
    char *v = zhash_lookup (ctx.pmi.kvs, key);
    if (pmi_simple_server_kvs_get_complete (ctx.pmi.srv, client, v) < 0)
        log_err_exit ("pmi_simple_server_kvs_get_complete");
    return 0;
}

int execvp_argz (char *argz, size_t argz_len)
{
    char **av = malloc (sizeof (char *) * (argz_count (argz, argz_len) + 1));
    if (!av) {
        errno = ENOMEM;
        return -1;
    }
    argz_extract (argz, argz_len, av);
    execvp (av[0], av);
    free (av);
    return -1;
}

/* Directly exec() a single flux broker.  It is assumed that we
 * are running in an environment with an external PMI service, and the
 * broker will figure out how to bootstrap without any further aid from
 * flux-start.
 */
int exec_broker (const char *cmd_argz, size_t cmd_argz_len,
                 const char *broker_path)
{
    char *argz = NULL;
    size_t argz_len = 0;

    add_args_list (&argz, &argz_len, ctx.opts, "wrap");
    if (argz_add (&argz, &argz_len, broker_path) != 0)
        goto nomem;

    add_args_list (&argz, &argz_len, ctx.opts, "broker-opts");
    if (cmd_argz) {
        if (argz_append (&argz, &argz_len, cmd_argz, cmd_argz_len) != 0)
            goto nomem;
    }
    if (optparse_hasopt (ctx.opts, "verbose")) {
        char *cpy = malloc (argz_len);
        if (!cpy)
            goto nomem;
        memcpy (cpy, argz, argz_len);
        argz_stringify (cpy, argz_len, ' ');
        log_msg ("%s", cpy);
        free (cpy);
    }
    if (!optparse_hasopt (ctx.opts, "noexec")) {
        if (execvp_argz (argz, argz_len) < 0)
            goto error;
    }
    free (argz);
    return 0;
nomem:
    errno = ENOMEM;
error:
    free (argz);
    return -1;
}

struct client *client_create (const char *broker_path, const char *scratch_dir,
                              int rank, const char *cmd_argz, size_t cmd_argz_len)
{
    struct client *cli = xzmalloc (sizeof (*cli));
    int client_fd;
    char * argz = NULL;
    size_t argz_len = 0;

    cli->rank = rank;
    cli->fd = -1;
    if (!(cli->p = subprocess_create (ctx.sm)))
        goto fail;
    subprocess_set_context (cli->p, "cli", cli);
    subprocess_add_hook (cli->p, SUBPROCESS_COMPLETE, child_exit);
    subprocess_add_hook (cli->p, SUBPROCESS_STATUS, child_report);
    add_args_list (&argz, &argz_len, ctx.opts, "wrap");
    argz_add (&argz, &argz_len, broker_path);
    char *run_dir = xasprintf ("%s/%d", scratch_dir, rank);
    if (mkdir (run_dir, 0755) < 0)
        log_err_exit ("mkdir %s", run_dir);
    cleanup_push_string (cleanup_directory, run_dir);
    char *dir_arg = xasprintf ("--setattr=broker.rundir=%s", run_dir);
    argz_add (&argz, &argz_len, dir_arg);
    argz_add (&argz, &argz_len, "--setattr=tbon.endpoint=ipc://%B/req");
    free (run_dir);
    free (dir_arg);
    add_args_list (&argz, &argz_len, ctx.opts, "broker-opts");
    if (rank == 0 && cmd_argz)
        argz_append (&argz, &argz_len, cmd_argz, cmd_argz_len); /* must be last arg */

    subprocess_set_args_from_argz (cli->p, argz, argz_len);
    free (argz);

    subprocess_set_environ (cli->p, environ);

    if ((cli->fd = subprocess_socketpair (cli->p, &client_fd)) < 0)
        goto fail;
    subprocess_set_context (cli->p, "client", cli);
    cli->w = flux_fd_watcher_create (ctx.reactor, cli->fd, FLUX_POLLIN,
                                     pmi_simple_cb, cli);
    if (!cli->w)
        goto fail;
    flux_watcher_start (cli->w);
    subprocess_setenvf (cli->p, "PMI_FD", 1, "%d", client_fd);
    subprocess_setenvf (cli->p, "PMI_RANK", 1, "%d", rank);
    subprocess_setenvf (cli->p, "PMI_SIZE", 1, "%d", ctx.size);
    return cli;
fail:
    client_destroy (cli);
    return NULL;
}

void client_destroy (struct client *cli)
{
    if (cli) {
        flux_watcher_destroy (cli->w);
        if (cli->fd != -1)
            close (cli->fd);
        if (cli->p)
            subprocess_destroy (cli->p);
        free (cli);
    }
}

void client_dumpargs (struct client *cli)
{
    int i, argc = subprocess_get_argc (cli->p);
    char *az = NULL;
    size_t az_len = 0;
    int e;

    for (i = 0; i < argc; i++)
        if ((e = argz_add (&az, &az_len, subprocess_get_arg (cli->p, i))) != 0)
            log_errn_exit (e, "argz_add");
    argz_stringify (az, az_len, ' ');
    log_msg ("%d: %s", cli->rank, az);
    free (az);
}

void pmi_server_initialize (int flags, const char *session_id)
{
    struct pmi_simple_ops ops = {
        .kvs_put = pmi_kvs_put,
        .kvs_get = pmi_kvs_get,
        .barrier_enter = NULL,
        .response_send = pmi_response_send,
        .debug_trace = pmi_debug_trace,
    };
    int appnum = strtol (session_id, NULL, 10);
    if (!(ctx.pmi.kvs = zhash_new()))
        oom ();
    ctx.pmi.srv = pmi_simple_server_create (ops, appnum, ctx.size,
                                            ctx.size, "-", flags, NULL);
    if (!ctx.pmi.srv)
        log_err_exit ("pmi_simple_server_create");
}

void pmi_server_finalize (void)
{
    zhash_destroy (&ctx.pmi.kvs);
    pmi_simple_server_destroy (ctx.pmi.srv);
}

int client_run (struct client *cli)
{
    return subprocess_run (cli->p);
}

/* Start an internal PMI server, and then launch "size" number of
 * broker processes that inherit a file desciptor to the internal PMI
 * server.  They will use that to bootstrap.  Since the PMI server is
 * internal and the connections to it passed through inherited file
 * descriptors it implies that the brokers in this instance must all
 * be contained on one node.  This is mostly useful for testing purposes.
 */
int start_session (const char *cmd_argz, size_t cmd_argz_len,
                   const char *broker_path)
{
    struct client *cli;
    int rank;
    int flags = 0;
    char *session_id;
    char *scratch_dir;

    if (!(ctx.reactor = flux_reactor_create (FLUX_REACTOR_SIGCHLD)))
        log_err_exit ("flux_reactor_create");
    if (!(ctx.timer = flux_timer_watcher_create (ctx.reactor,
                                                  ctx.killer_timeout, 0.,
                                                  killer, NULL)))
        log_err_exit ("flux_timer_watcher_create");
    if (!(ctx.sm = subprocess_manager_create ()))
        log_err_exit ("subprocess_manager_create");
    if (subprocess_manager_set (ctx.sm, SM_REACTOR, ctx.reactor) < 0)
        log_err_exit ("subprocess_manager_set reactor");
    session_id = xasprintf ("%d", getpid ());

    if (optparse_hasopt (ctx.opts, "scratchdir"))
        scratch_dir = xstrdup (optparse_get_str (ctx.opts, "scratchdir", NULL));
    else
        scratch_dir = create_scratch_dir (session_id);

    if (optparse_hasopt (ctx.opts, "trace-pmi-server"))
        flags |= PMI_SIMPLE_SERVER_TRACE;

    pmi_server_initialize (flags, session_id);

    for (rank = 0; rank < ctx.size; rank++) {
        if (!(cli = client_create (broker_path, scratch_dir, rank,
                                   cmd_argz, cmd_argz_len)))
            log_err_exit ("client_create");
        if (optparse_hasopt (ctx.opts, "verbose"))
            client_dumpargs (cli);
        if (optparse_hasopt (ctx.opts, "noexec")) {
            client_destroy (cli);
            continue;
        }
        if (client_run (cli) < 0)
            log_err_exit ("subprocess_run");
        ctx.count++;
    }
    if (flux_reactor_run (ctx.reactor, 0) < 0)
        log_err_exit ("flux_reactor_run");

    pmi_server_finalize ();

    free (session_id);
    free (scratch_dir);

    subprocess_manager_destroy (ctx.sm);
    flux_watcher_destroy (ctx.timer);
    flux_reactor_destroy (ctx.reactor);

    return (ctx.exit_rc);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
