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
#include <limits.h>
#include <termios.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <libgen.h>
#include <signal.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif
#include <sys/ioctl.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/optparse.h>
#include <flux/taskmap.h>

#include "ccan/str/str.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/setenvf.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libpmi/simple_server.h"
#include "src/common/libhostlist/hostlist.h"
#include "src/common/librouter/usock_service.h"
#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"

#define DEFAULT_EXIT_TIMEOUT 20.0

extern char **environ;

static struct {
    struct termios saved_termios;
    double exit_timeout;
    const char *exit_mode;
    const char *start_mode;
    flux_reactor_t *reactor;
    flux_watcher_t *timer;
    bool shutdown;
    zlist_t *clients;
    optparse_t *opts;
    int verbose;
    int test_size;
    int exit_rc;
    struct {
        zhash_t *kvs;
        struct pmi_simple_server *srv;
    } pmi;
    flux_t *h;
    flux_msg_handler_t **handlers;
} ctx;

struct client {
    int rank;
    flux_subprocess_t *p;
    flux_cmd_t *cmd;
    int exit_rc;
    const flux_msg_t *wait_request;
    const flux_msg_t *run_request;
};

void exit_timeout (flux_reactor_t *r,
                   flux_watcher_t *w,
                   int revents,
                   void *arg);
int start_session (const char *cmd_argz,
                   size_t cmd_argz_len,
                   const char *broker_path);
int exec_broker (const char *cmd_argz,
                 size_t cmd_argz_len,
                 const char *broker_path);
char *create_rundir (void);
void client_destroy (struct client *cli);
char *find_broker (const char *searchpath);
static void client_wait_respond (struct client *cli);
static void client_run_respond (struct client *cli, int errnum);

const char *default_config_path = X_SYSCONFDIR "/flux/system/conf.d";
const char *default_statedir = "/var/lib/flux";

const char *usage_msg = "[OPTIONS] command ...";
static struct optparse_option opts[] = {
    { .name = "setattr",    .key = 'S', .has_arg = 1, .arginfo = "ATTR=VAL",
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "Set broker attribute", },
    { .name = "config-path",.key = 'c', .has_arg = 1, .arginfo = "PATH",
      .usage = "Set broker config from PATH (default: none)", },
    { .name = "recovery",   .key = 'r', .has_arg = 2, .arginfo = "[TARGET]",
      .flags = OPTPARSE_OPT_SHORTOPT_OPTIONAL_ARG,
      .usage = "Start instance in recovery mode with dump file or statedir", },
    { .name = "sysconfig",  .has_arg = 0,
      .usage = "Load system configuration", },
    { .name = "verbose",    .key = 'v', .has_arg = 2, .arginfo = "[LEVEL]",
      .usage = "Be annoyingly informative by degrees", },
    { .name = "noexec",     .key = 'X', .has_arg = 0,
      .usage = "Don't execute (useful with -v, --verbose)", },
    { .name = "broker-opts",.key = 'o', .has_arg = 1, .arginfo = "OPTS",
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "Add comma-separated broker options, e.g. \"-o,-v\"", },
    /* Option group 1, these options will be listed after those above */
    { .group = 1,
      .name = "wrap", .has_arg = 1, .arginfo = "ARGS,...",
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "Wrap broker execution in comma-separated arguments"
    },
    /* Option group 2 */
    { .group = 2,
      .usage = "\nOptions useful for testing:" },
    { .group = 2,
      .name = "test-size",       .key = 's', .has_arg = 1, .arginfo = "N",
      .usage = "Start a test instance by launching N brokers locally", },
    { .group = 2,
      .name = "test-hosts", .has_arg = 1, .arginfo = "HOSTLIST",
      .usage = "Set FLUX_FAKE_HOSTNAME in environment of each broker", },
    { .group = 2,
      .name = "test-exit-timeout", .has_arg = 1, .arginfo = "FSD",
      .usage = "After a broker exits, kill other brokers after timeout", },
    { .group = 2,
      .name = "test-exit-mode", .has_arg = 1, .arginfo = "any|leader",
      .usage = "Trigger exit timer on leader/any broker exit (default=any)", },
    { .group = 2,
      .name = "test-start-mode", .has_arg = 1, .arginfo = "all|leader",
      .usage = "Start all brokers immediately or just leader (default=all)", },
    { .group = 2,
      .name = "test-rundir", .has_arg = 1, .arginfo = "DIR",
      .usage = "Use DIR as broker run directory", },
    { .group = 2,
      .name = "test-rundir-cleanup", .has_arg = 0,
      .usage = "Clean up --test-rundir DIR upon flux-start completion", },
    { .group = 2,
      .name = "test-pmi-clique",
      .has_arg = 1,
      .arginfo = "single|per-broker|none|TASKMAP",
      .usage = "Set PMI_process_mapping mode (default=single)", },
    { .flags = OPTPARSE_OPT_HIDDEN,
      .name = "killer-timeout", .has_arg = 1, .arginfo = "FSD",
      .usage = "(deprecated)" },
    OPTPARSE_TABLE_END,
};

/* Various things will go wrong with module loading, process execution, etc.
 *  when current directory can't be found. Exit early with error to avoid
 *  chaotic stream of error messages later in startup.
 */
static void sanity_check_working_directory (void)
{
    char buf [PATH_MAX+1024];
    if (!getcwd (buf, sizeof (buf)))
        log_err_exit ("Unable to get current working directory");
}

int main (int argc, char *argv[])
{
    int e, status = 0;
    char *command = NULL;
    size_t len = 0;
    const char *searchpath;
    int optindex;
    char *broker_path;

    log_init ("flux-start");

    sanity_check_working_directory ();

    if (!(ctx.opts = optparse_create ("flux-start"))
        || optparse_add_option_table (ctx.opts, opts) != OPTPARSE_SUCCESS
        || optparse_set (ctx.opts,
                         OPTPARSE_OPTION_WIDTH,
                         32) != OPTPARSE_SUCCESS
        || optparse_set (ctx.opts,
                         OPTPARSE_USAGE,
                         usage_msg) != OPTPARSE_SUCCESS)
        log_msg_exit ("error setting up option parsing");
    if ((optindex = optparse_parse_args (ctx.opts, argc, argv)) < 0)
        exit (1);

    ctx.exit_timeout = optparse_get_duration (ctx.opts,
                                              "test-exit-timeout",
                                              DEFAULT_EXIT_TIMEOUT);
    if (!optparse_hasopt (ctx.opts, "test-exit-timeout"))
        ctx.exit_timeout = optparse_get_duration (ctx.opts,
                                                  "killer-timeout",
                                                  ctx.exit_timeout);

    ctx.exit_mode = optparse_get_str (ctx.opts, "test-exit-mode", "any");
    if (!streq (ctx.exit_mode, "any")
        && !streq (ctx.exit_mode, "leader"))
        log_msg_exit ("unknown --test-exit-mode: %s", ctx.exit_mode);

    ctx.start_mode = optparse_get_str (ctx.opts, "test-start-mode", "all");
    if (!streq (ctx.start_mode, "all")
        && !streq (ctx.start_mode, "leader"))
        log_msg_exit ("unknown --test-start-mode: %s", ctx.start_mode);

    ctx.verbose = optparse_get_int (ctx.opts, "verbose", 0);

    if (optindex < argc) {
        if ((e = argz_create (argv + optindex, &command, &len)) != 0)
            log_errn_exit (e, "argz_create");
    }

    if (!(searchpath = getenv ("FLUX_EXEC_PATH")))
        log_msg_exit ("FLUX_EXEC_PATH is not set");
    if (!(broker_path = find_broker (searchpath)))
        log_msg_exit ("Could not locate broker in %s", searchpath);

    if (optparse_hasopt (ctx.opts, "test-size")) {
        ctx.test_size = optparse_get_int (ctx.opts, "test-size", -1);
        if (ctx.test_size <= 0)
            log_msg_exit ("--test-size argument must be > 0");
    }

    if (!optparse_hasopt (ctx.opts, "test-size")) {
        int i;
        for (i = 0; i < ARRAY_SIZE (opts); i++) {
            if (opts[i].name
                && strstarts (opts[i].name, "test-")
                && optparse_hasopt (ctx.opts, opts[i].name))
                log_msg_exit ("--%s only works with --test-size", opts[i].name);
        }
    }

    if (!optparse_hasopt (ctx.opts, "test-size")) {
        if (exec_broker (command, len, broker_path) < 0)
            log_err_exit ("error execing broker");
    }
    else {
        status = start_session (command, len, broker_path);
    }

    optparse_destroy (ctx.opts);
    free (broker_path);

    if (command)
        free (command);

    log_fini ();

    return status;
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

void exit_timeout (flux_reactor_t *r,
                   flux_watcher_t *w,
                   int revents, void *arg)
{
    struct client *cli;
    struct idset *ids;

    ids = idset_create (ctx.test_size, 0);
    cli = zlist_first (ctx.clients);
    while (cli) {
        if (cli->p) {
            flux_future_t *f = flux_subprocess_kill (cli->p, SIGKILL);
            if (f && ids)
                idset_set (ids, cli->rank);
            flux_future_destroy (f);
        }
        cli = zlist_next (ctx.clients);
    }
    if (ids && idset_count (ids) > 0) {
        char *s = idset_encode (ids, IDSET_FLAG_RANGE);
        log_msg ("Exit timeout: killed rank %s", s ? s : "null");
        free (s);
    }
    idset_destroy (ids);
}

void update_timer (void)
{
    struct client *cli;
    int count = 0;
    bool leader_exit = false;
    bool shutdown = false;

    cli = zlist_first (ctx.clients);
    while (cli) {
        if (cli->p)
            count++;
        if (cli->rank == 0 && !cli->p)
            leader_exit = true;
        cli = zlist_next (ctx.clients);
    }
    if (streq (ctx.exit_mode, "any")) {
        if (count > 0 && count < ctx.test_size)
            shutdown = true;
    }
    else if (streq (ctx.exit_mode, "leader")) {
        if (count > 0 && leader_exit)
            shutdown = true;
    }
    if (shutdown && !ctx.shutdown)
        flux_watcher_start (ctx.timer);
    else if (!shutdown && ctx.shutdown)
        flux_watcher_stop (ctx.timer);
    ctx.shutdown = shutdown;
}

static void completion_cb (flux_subprocess_t *p)
{
    struct client *cli = flux_subprocess_aux_get (p, "cli");

    assert (cli);

    if ((cli->exit_rc = flux_subprocess_exit_code (p)) < 0) {
        /* bash standard, signals + 128 */
        if ((cli->exit_rc = flux_subprocess_signaled (p)) >= 0)
            cli->exit_rc += 128;
    }

    /* In 'any' mode, the highest of the broker exit codes is
     * flux-start's exit code.  In 'leader' mode, the leader broker's
     * exit code is flux-start's exit code.
     */
    if (streq (ctx.exit_mode, "any")) {
        if (cli->exit_rc > ctx.exit_rc)
            ctx.exit_rc = cli->exit_rc;
    }
    else if (streq (ctx.exit_mode, "leader")) {
        if (cli->rank == 0)
            ctx.exit_rc = cli->exit_rc;
    }

    flux_subprocess_destroy (cli->p);
    cli->p = NULL;
    client_wait_respond (cli);
    update_timer ();
}

static void state_cb (flux_subprocess_t *p, flux_subprocess_state_t state)
{
    struct client *cli = flux_subprocess_aux_get (p, "cli");

    assert (cli);

    switch (state) {
        case FLUX_SUBPROCESS_INIT:
            break;
        case FLUX_SUBPROCESS_RUNNING:
            client_run_respond (cli, 0);
            break;
        case FLUX_SUBPROCESS_FAILED: { // completion will not be called
            log_errn_exit (flux_subprocess_fail_errno (p),
                           "%d subprocess failed",
                           cli->rank);
            break;
        }
        case FLUX_SUBPROCESS_EXITED: {
            pid_t pid = flux_subprocess_pid (p);
            int status = flux_subprocess_status (p);

            assert (status >= 0);
            if (WIFSIGNALED (status)) {
                log_msg ("%d (pid %d) %s",
                         cli->rank,
                         pid,
                         strsignal (WTERMSIG (status)));
            }
            else if (WIFEXITED (status) && WEXITSTATUS (status) != 0) {
                log_msg ("%d (pid %d) exited with rc=%d",
                         cli->rank,
                         pid,
                         WEXITSTATUS (status));
            }
            break;
        }
        case FLUX_SUBPROCESS_STOPPED:
            /* ignore */
            break;
    }
}

void channel_cb (flux_subprocess_t *p, const char *stream)
{
    struct client *cli = flux_subprocess_aux_get (p, "cli");
    const char *buf;
    int rc, len;

    assert (cli);
    assert (streq (stream, "PMI_FD"));

    if ((len = flux_subprocess_read_line (p, stream, &buf)) < 0)
        log_err_exit ("%s: flux_subprocess_read_line", __FUNCTION__);

    if (len) {
        rc = pmi_simple_server_request (ctx.pmi.srv, buf, cli, cli->rank);
        if (rc < 0)
            log_err_exit ("%s: pmi_simple_server_request", __FUNCTION__);
        if (rc == 1)
            (void) flux_subprocess_close (p, stream);
    }
}

void add_argzf (char **argz, size_t *argz_len, const char *fmt, ...)
{
    va_list ap;
    char *arg = NULL;

    va_start (ap, fmt);
    if (vasprintf (&arg, fmt, ap) < 0)
        log_err_exit ("vasprintf");
    va_end (ap);
    if (argz_add (argz, argz_len, arg) != 0)
        log_err_exit ("argz_add");
    free (arg);
}

void add_args_list (char **argz,
                    size_t *argz_len,
                    optparse_t *opt,
                    const char *name,
                    const char *prepend)
{
    const char *arg;
    optparse_getopt_iterator_reset (opt, name);
    while ((arg = optparse_getopt_next (opt, name)))
        add_argzf (argz, argz_len, "%s%s", prepend, arg);
}

char *create_rundir (void)
{
    char *tmpdir = getenv ("TMPDIR");
    char *rundir = xasprintf ("%s/flux-XXXXXX", tmpdir ? tmpdir : "/tmp");

    if (!mkdtemp (rundir))
        log_err_exit ("mkdtemp %s", rundir);
    return rundir;
}

static int pmi_response_send (void *client, const char *buf)
{
    struct client *cli = client;
    return flux_subprocess_write (cli->p, "PMI_FD", buf, strlen (buf));
}

static void pmi_debug_trace (void *client, const char *buf)
{
    struct client *cli = client;
    fprintf (stderr, "%d: %s", cli->rank, buf);
}

int pmi_kvs_put (void *arg,
                 const char *kvsname,
                 const char *key,
                 const char *val)
{
    zhash_update (ctx.pmi.kvs, key, xstrdup (val));
    zhash_freefn (ctx.pmi.kvs, key, (zhash_free_fn *)free);
    return 0;
}

int pmi_kvs_get (void *arg,
                 void *client,
                 const char *kvsname,
                 const char *key)
{
    char *v = zhash_lookup (ctx.pmi.kvs, key);
    if (pmi_simple_server_kvs_get_complete (ctx.pmi.srv, client, v) < 0)
        log_err_exit ("pmi_simple_server_kvs_get_complete");
    return 0;
}

void pmi_abort (void *arg,
                void *client,
                int exit_code,
                const char *error_message)
{
    struct client *cli = client;

    log_msg ("%d: PMI_Abort()%s%s",
             cli->rank,
             error_message ? ": " : "",
             error_message ? error_message : "");

    cli = zlist_first (ctx.clients);
    while (cli) {
        if (cli->p) {
            flux_future_t *f = flux_subprocess_kill (cli->p, SIGKILL);
            flux_future_destroy (f);
        }
        cli = zlist_next (ctx.clients);
    }
}

int execvp_argz (char *argz, size_t argz_len)
{
    char **av = malloc (sizeof (char *) * (argz_count (argz, argz_len) + 1));
    if (!av)
        return -1;
    argz_extract (argz, argz_len, av);
    execvp (av[0], av);
    free (av);
    return -1;
}

bool system_instance_is_running (void)
{
    flux_t *h;
    bool running = false;

    unsetenv ("FLUX_URI");
    if ((h = flux_open (NULL, 0))) {
        running = true;
        flux_close (h);
    }
    return running;
}

void process_recovery_option (char **argz,
                              size_t *argz_len,
                              bool *system_recovery)
{
    char path[1024];
    const char *optarg;
    struct stat sb;

    add_argzf (argz, argz_len, "-Sbroker.recovery-mode=1");
    add_argzf (argz, argz_len, "-Sbroker.quorum=1");
    add_argzf (argz, argz_len, "-Sbroker.module-nopanic=1");
    add_argzf (argz, argz_len, "-Slog-stderr-level=5");

    // if --recovery has no optional argument, assume this is the system
    // instance and make sure it is not running.
    if (!(optarg = optparse_get_str (ctx.opts, "recovery", NULL))) {
        optarg = default_statedir;
        if (system_instance_is_running ())
            log_msg_exit ("system instance is already running");
        *system_recovery = true;
    }

    // if argument is a dir, assume statedir; if file, assume dump archive
    if (stat (optarg, &sb) < 0)
        log_err_exit ("%s", optarg);
    if (S_ISDIR (sb.st_mode)) {
        if (sb.st_uid != getuid ())
            log_msg_exit ("%s: not owned by you", optarg);
        if ((sb.st_mode & S_IRWXU) != S_IRWXU)
            log_msg_exit ("%s: no access", optarg);
        snprintf (path, sizeof (path), "%s/content.sqlite", optarg);
        if (access (path, F_OK) < 0)
            log_err_exit ("%s", path);
        if (access (path, R_OK) < 0)
            log_msg_exit ("%s: no read permission", path);
        if (access (path, W_OK) < 0)
            log_msg_exit ("%s: no write permission", path);
        add_argzf (argz, argz_len, "-Sstatedir=%s", optarg);
    }
    else
        add_argzf (argz, argz_len, "-Scontent.restore=%s", optarg);
}

int add_args_common (char **argz,
                     size_t *argz_len,
                     const char *broker_path)
{
    bool system_recovery = false;
    const char *config_path = NULL;

    add_args_list (argz, argz_len, ctx.opts, "wrap", "");
    if (argz_add (argz, argz_len, broker_path) != 0) {
        errno = ENOMEM;
        return -1;
    }
    add_args_list (argz, argz_len, ctx.opts, "setattr", "-S");
    add_args_list (argz, argz_len, ctx.opts, "broker-opts", "");

    if (optparse_hasopt (ctx.opts, "recovery"))
        process_recovery_option (argz, argz_len, &system_recovery);

    if (system_recovery || optparse_hasopt (ctx.opts, "sysconfig"))
        config_path = default_config_path;
    if (optparse_hasopt (ctx.opts, "config-path")) {
        const char *conf = optparse_get_str (ctx.opts, "config-path", NULL);
        if (config_path)
            log_msg ("Warning: overriding recovery/--sysconfig path with %s",
                     conf);
        config_path = conf;
    }
    if (config_path)
        add_argzf (argz, argz_len, "-c%s", config_path);

    return 0;
}

/* Directly exec() a single flux broker.  It is assumed that we
 * are running in an environment with an external PMI service, and the
 * broker will figure out how to bootstrap without any further aid from
 * flux-start.
 */
int exec_broker (const char *cmd_argz,
                 size_t cmd_argz_len,
                 const char *broker_path)
{
    char *argz = NULL;
    size_t argz_len = 0;

    if (add_args_common (&argz, &argz_len, broker_path) < 0)
        goto error;

    if (cmd_argz) {
        if (argz_append (&argz, &argz_len, cmd_argz, cmd_argz_len) != 0)
            goto nomem;
    }
    if (ctx.verbose >= 1) {
        char *cpy = malloc (argz_len);
        if (!cpy)
            goto error;
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
    ERRNO_SAFE_WRAP (free, argz);
    return -1;
}

struct client *client_create (const char *broker_path,
                              const char *rundir,
                              int rank,
                              const char *cmd_argz,
                              size_t cmd_argz_len,
                              const char *hostname)
{
    struct client *cli = xzmalloc (sizeof (*cli));
    char *arg;
    char *argz = NULL;
    size_t argz_len = 0;

    cli->rank = rank;

    if (add_args_common (&argz, &argz_len, broker_path) < 0)
        goto fail;

    add_argzf (&argz, &argz_len, "--setattr=rundir=%s", rundir);

    if (rank == 0 && cmd_argz) /* must be last arg */
        argz_append (&argz, &argz_len, cmd_argz, cmd_argz_len);

    if (!(cli->cmd = flux_cmd_create (0, NULL, environ)))
        goto fail;
    arg = argz_next (argz, argz_len, NULL);
    while (arg) {
        if (flux_cmd_argv_append (cli->cmd, arg) < 0)
            log_err_exit ("flux_cmd_argv_append");
        arg = argz_next (argz, argz_len, arg);
    }
    free (argz);

    if (flux_cmd_add_channel (cli->cmd, "PMI_FD") < 0)
        log_err_exit ("flux_cmd_add_channel");
    if (flux_cmd_setenvf (cli->cmd, 1, "PMI_RANK", "%d", rank) < 0
        || flux_cmd_setenvf (cli->cmd, 1, "PMI_SIZE", "%d", ctx.test_size) < 0
        || flux_cmd_setenvf (cli->cmd,
                             1,
                             "FLUX_START_URI",
                             "local://%s/start",
                             rundir) < 0
        || (hostname && flux_cmd_setenvf (cli->cmd,
                                          1,
                                          "FLUX_FAKE_HOSTNAME",
                                          "%s",
                                          hostname) < 0))
            log_err_exit ("error setting up environment for rank %d", rank);
    return cli;
fail:
    ERRNO_SAFE_WRAP (free, argz);
    ERRNO_SAFE_WRAP (client_destroy, cli);
    return NULL;
}

void client_destroy (struct client *cli)
{
    if (cli) {
        if (cli->p)
            flux_subprocess_destroy (cli->p);
        if (cli->cmd)
            flux_cmd_destroy (cli->cmd);
        free (cli);
    }
}

void client_dumpargs (struct client *cli)
{
    int i, argc = flux_cmd_argc (cli->cmd);
    char *az = NULL;
    size_t az_len = 0;
    int e;

    for (i = 0; i < argc; i++)
        if ((e = argz_add (&az, &az_len, flux_cmd_arg (cli->cmd, i))) != 0)
            log_errn_exit (e, "argz_add");
    argz_stringify (az, az_len, ' ');
    log_msg ("%d: %s", cli->rank, az);
    free (az);
}

void pmi_server_initialize (int flags)
{
    struct taskmap *map;
    const char *mode = optparse_get_str (ctx.opts,
                                         "test-pmi-clique",
                                         "single");
    struct pmi_simple_ops ops = {
        .abort = pmi_abort,
        .kvs_put = pmi_kvs_put,
        .kvs_get = pmi_kvs_get,
        .barrier_enter = NULL,
        .response_send = pmi_response_send,
        .debug_trace = pmi_debug_trace,
    };
    int appnum = 0;
    flux_error_t error;

    if (!(ctx.pmi.kvs = zhash_new ())
        || !(map = taskmap_create ()))
        oom ();

    if (streq (mode, "single")) {
        if (taskmap_append (map, 0, 1, ctx.test_size) < 0)
            log_err_exit ("error encoding PMI_process_mapping");
    }
    else if (streq (mode, "per-broker")) {
        if (taskmap_append (map, 0, ctx.test_size, 1) < 0)
            log_err_exit ("error encoding PMI_process_mapping");
    }
    else if (streq (mode, "none")) {
        ; // do nothing
    }
    else {
        taskmap_destroy (map);
        if (!(map = taskmap_decode (mode, &error)))
            log_msg_exit ("test-pmi-clique: %s", error.text);
    }

    if (taskmap_nnodes (map) > 0) {
        char *s;
        if (!(s = taskmap_encode (map, TASKMAP_ENCODE_PMI)))
            log_msg_exit ("error encoding PMI_process_mapping");
        zhash_update (ctx.pmi.kvs, "PMI_process_mapping", s);
    }
    ctx.pmi.srv = pmi_simple_server_create (ops,
                                            appnum,
                                            ctx.test_size,
                                            ctx.test_size,
                                            "-",
                                            flags,
                                            NULL);
    if (!ctx.pmi.srv)
        log_err_exit ("pmi_simple_server_create");
    taskmap_destroy (map);
}

void pmi_server_finalize (void)
{
    zhash_destroy (&ctx.pmi.kvs);
    pmi_simple_server_destroy (ctx.pmi.srv);
}

int client_run (struct client *cli)
{
    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_state_change = state_cb,
        .on_channel_out = channel_cb,
        .on_stdout = NULL,
        .on_stderr = NULL,
    };
    int flags = 0;
    if (cli->p) {
        errno = EEXIST;
        return -1;
    }
    /* We want stdio fallthrough so subprocess can capture tty if
     * necessary (i.e. an interactive shell)
     */
    flags |= FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH;
    flags |= FLUX_SUBPROCESS_FLAGS_NO_SETPGRP;
    if (!(cli->p = flux_local_exec (ctx.reactor,
                                    flags,
                                    cli->cmd,
                                    &ops)))
        log_err_exit ("flux_exec");
    if (flux_subprocess_aux_set (cli->p, "cli", cli, NULL) < 0)
        log_err_exit ("flux_subprocess_aux_set");
    return 0;
}

void restore_termios (void)
{
    if (tcsetattr (STDIN_FILENO, TCSAFLUSH, &ctx.saved_termios) < 0)
        log_err ("tcsetattr");
}

void status_cb (flux_t *h,
                flux_msg_handler_t *mh,
                const flux_msg_t *msg,
                void *arg)
{
    struct client *cli;
    json_t *procs = NULL;

    if (!(procs = json_array()))
        goto nomem;
    cli = zlist_first (ctx.clients);
    while (cli) {
        json_t *entry;

        if (!(entry = json_pack ("{s:i}",
                                 "pid", flux_subprocess_pid (cli->p))))
            goto nomem;
        if (json_array_append_new (procs, entry) < 0) {
            json_decref (entry);
            goto nomem;
        }
        cli = zlist_next (ctx.clients);
    }
    if (flux_respond_pack (h, msg, "{s:O}", "procs", procs) < 0)
        log_err ("error responding to status request");
    json_decref (procs);
    return;
nomem:
    errno = ENOMEM;
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        log_err ("error responding to status request");
    json_decref (procs);
}

static struct client *client_lookup (int rank)
{
    struct client *cli;

    cli = zlist_first (ctx.clients);
    while (cli) {
        if (cli->rank == rank)
            return cli;
        cli = zlist_next (ctx.clients);
    }
    errno = ESRCH;
    return NULL;
}

/* Send 'signum' to 'cli'.  Since this is always a local operation,
 * the future is immediately fulfilled, so just destroy it.
 * If cli is not running, return success.
 */
static int client_kill (struct client *cli, int signum)
{
    flux_future_t *f;
    if (!cli->p)
        return 0;
    if (!(f = flux_subprocess_kill (cli->p, signum)))
        return -1;
    flux_future_destroy (f);
    return 0;
}

/* Respond with errum result to pending run request, if any.
 */
static void client_run_respond (struct client *cli, int errnum)
{
    if (cli->run_request) {
        int rc;
        if (errnum == 0)
            rc = flux_respond (ctx.h, cli->run_request, NULL);
        else
            rc = flux_respond_error (ctx.h, cli->run_request, errnum, NULL);
        if (rc < 0)
            log_err ("error responding to start.run request");
        flux_msg_decref (cli->run_request);
        cli->run_request = NULL;
    }
}

static void client_wait_respond (struct client *cli)
{
    if (cli->wait_request) {
        if (flux_respond_pack (ctx.h,
                               cli->wait_request,
                               "{s:i}",
                               "exit_rc", cli->exit_rc) < 0)
            log_err ("error responding to wait request");
        flux_msg_decref (cli->wait_request);
        cli->wait_request = NULL;
    }
}

/* Send signal to one broker by rank.
 */
void kill_cb (flux_t *h,
              flux_msg_handler_t *mh,
              const flux_msg_t *msg,
              void *arg)
{
    int rank;
    int signum;
    struct client *cli;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:i s:i}",
                             "rank", &rank,
                             "signum", &signum) < 0)
        goto error;
    if (!(cli = client_lookup (rank)))
        goto error;
    if (client_kill (cli, signum) < 0)
        goto error;
    if (flux_respond (h, msg, NULL) < 0)
        log_err ("error responding to kill request");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        log_err ("error responding to kill request");
}

/* Wait for one broker to complete and return its exit_rc.
 * If the child is not running, return cli->exit_rc immediately.  Otherwise,
 * the request is parked on the 'struct child' (one request allowed per child),
 * and response is sent by completion handler upon broker completion.
 */
void wait_cb (flux_t *h,
              flux_msg_handler_t *mh,
              const flux_msg_t *msg,
              void *arg)
{
    int rank;
    struct client *cli;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:i}",
                             "rank", &rank) < 0)
        goto error;
    if (!(cli = client_lookup (rank)))
        goto error;
    if (cli->wait_request) {
        errno = EEXIST;
        goto error;
    }
    cli->wait_request = flux_msg_incref (msg);
    if (!cli->p)
        client_wait_respond (cli);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        log_err ("error responding to start request");
}

/* Run one broker by rank.
 */
void run_cb (flux_t *h,
             flux_msg_handler_t *mh,
             const flux_msg_t *msg,
             void *arg)
{
    int rank;
    struct client *cli;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:i}",
                             "rank", &rank) < 0)
        goto error;
    if (!(cli = client_lookup (rank)))
        goto error;
    if (cli->run_request) {
        errno = EEXIST;
        goto error;
    }
    if (client_run (cli) < 0)
        goto error;
    cli->run_request = flux_msg_incref (msg);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        log_err ("error responding to start request");
}

void disconnect_cb (flux_t *h,
                    flux_msg_handler_t *mh,
                    const flux_msg_t *msg,
                    void *arg)
{
    const char *uuid;

    if (!(uuid = flux_msg_route_first (msg)))
        return;
    if (ctx.verbose >= 1)
        log_msg ("disconnect from %.5s", uuid);
}

const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "start.status", status_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "start.kill", kill_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "start.wait", wait_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "start.run", run_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "disconnect", disconnect_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

/* Set up test-related RPC handlers on local://${rundir}/start
 * Ensure that service-related reactor watchers do not contribute to the
 * reactor usecount, since the reactor is expected to exit once the
 * subprocesses are complete.
 */
void start_server_initialize (const char *rundir, bool verbose)
{
    char path[1024];
    if (snprintf (path, sizeof (path), "%s/start", rundir) >= sizeof (path))
        log_msg_exit ("internal buffer overflow");
    if (!(ctx.h = usock_service_create (ctx.reactor, path, verbose)))
        log_err_exit ("could not created embedded flux-start server");
    if (flux_msg_handler_addvec (ctx.h, htab, NULL, &ctx.handlers) < 0)
        log_err_exit ("could not register service methods");

    flux_watcher_unref (flux_get_handle_watcher (ctx.h));
    flux_watcher_unref (usock_service_listen_watcher (ctx.h));
}

void start_server_finalize (void)
{
    flux_msg_handler_delvec (ctx.handlers);
    flux_close (ctx.h);
}

/* Start an internal PMI server, and then launch the requested number of
 * broker processes that inherit a file descriptor to the internal PMI
 * server.  They will use that to bootstrap.  Since the PMI server is
 * internal and the connections to it passed through inherited file
 * descriptors it implies that the brokers in this instance must all
 * be contained on one node.  This is mostly useful for testing purposes.
 */
int start_session (const char *cmd_argz,
                   size_t cmd_argz_len,
                   const char *broker_path)
{
    struct client *cli;
    int rank;
    int flags = 0;
    char *rundir;
    struct hostlist *hosts = NULL;

    if (isatty (STDIN_FILENO)) {
        if (tcgetattr (STDIN_FILENO, &ctx.saved_termios) < 0)
            log_err_exit ("tcgetattr");
        if (atexit (restore_termios) != 0)
            log_err_exit ("atexit");
        if (signal (SIGTTOU, SIG_IGN) == SIG_ERR)
            log_err_exit ("signal");
    }
    if (!(ctx.reactor = flux_reactor_create (0)))
        log_err_exit ("flux_reactor_create");
    if (!(ctx.timer = flux_timer_watcher_create (ctx.reactor,
                                                 ctx.exit_timeout,
                                                 0.,
                                                 exit_timeout,
                                                 NULL)))
        log_err_exit ("flux_timer_watcher_create");
    if (!(ctx.clients = zlist_new ()))
        log_err_exit ("zlist_new");

    if (optparse_hasopt (ctx.opts, "test-rundir")) {
        struct stat sb;
        rundir = xstrdup (optparse_get_str (ctx.opts, "test-rundir", NULL));
        if (stat (rundir, &sb) < 0)
            log_err_exit ("%s", rundir);
        if (!S_ISDIR (sb.st_mode))
            log_msg_exit ("%s: not a directory", rundir);
    }
    else
        rundir = create_rundir ();
    /* Clean up rundir upon flux-start completion if we created it,
     * or if cleanup was explicitly requested.
     */
    if (!optparse_hasopt (ctx.opts, "test-rundir")
        || optparse_hasopt (ctx.opts, "test-rundir-cleanup"))
        cleanup_push_string (cleanup_directory_recursive, rundir);

    start_server_initialize (rundir, ctx.verbose >= 1 ? true : false);

    if (ctx.verbose >= 2)
        flags |= PMI_SIMPLE_SERVER_TRACE;

    pmi_server_initialize (flags);

    if (optparse_hasopt (ctx.opts, "test-hosts")) {
        const char *s = optparse_get_str (ctx.opts, "test-hosts", NULL);
        if (!(hosts = hostlist_decode (s)))
            log_msg_exit ("could not decode --test-hosts hostlist");
        if (hostlist_count (hosts) != ctx.test_size)
            log_msg_exit ("--test-hosts hostlist has incorrect size");
    }

    for (rank = 0; rank < ctx.test_size; rank++) {
        if (!(cli = client_create (broker_path,
                                   rundir,
                                   rank,
                                   cmd_argz,
                                   cmd_argz_len,
                                   hosts ? hostlist_nth (hosts, rank) : NULL)))
            log_err_exit ("client_create");
        if (ctx.verbose >= 1)
            client_dumpargs (cli);
        if (optparse_hasopt (ctx.opts, "noexec")) {
            client_destroy (cli);
            continue;
        }
        if (zlist_append (ctx.clients, cli) < 0)
            log_err_exit ("zlist_append");
    }
    if (streq (ctx.start_mode, "leader")) {
        cli = zlist_first (ctx.clients);
        if (client_run (cli) < 0)
            log_err_exit ("client_run");
    }
    else if (streq (ctx.start_mode, "all")) {
        cli = zlist_first (ctx.clients);
        while (cli) {
            if (client_run (cli) < 0)
                log_err_exit ("client_run");
            cli = zlist_next (ctx.clients);
        }
    }
    if (flux_reactor_run (ctx.reactor, 0) < 0)
        log_err_exit ("flux_reactor_run");

    pmi_server_finalize ();
    start_server_finalize ();

    hostlist_destroy (hosts);
    free (rundir);

    if (ctx.clients) {
        while ((cli = zlist_pop (ctx.clients)))
            client_destroy (cli);
        zlist_destroy (&ctx.clients);
    }
    flux_watcher_destroy (ctx.timer);
    flux_reactor_destroy (ctx.reactor);

    return (ctx.exit_rc);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
