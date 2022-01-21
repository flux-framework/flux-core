/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
# include "config.h"
#endif
#include "builtin.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <stdbool.h>
#include <argz.h>
#include <glob.h>
#include <inttypes.h>
#include <termios.h>

#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/uri.h"
#include "src/common/librouter/usock.h"
#include "src/common/librouter/router.h"


struct proxy_command {
    struct usock_server *server;
    struct router *router;
    flux_t *h;
    flux_subprocess_t *p;
    int exit_code;
    uid_t proxy_user;
};

static const char *route_auxkey = "flux::route";

static struct termios orig_term;
static bool term_needs_restore = false;

static void save_terminal_state (void)
{
    if (isatty (STDIN_FILENO)) {
        tcgetattr (STDIN_FILENO, &orig_term);
        term_needs_restore = true;
    }
}

static void restore_terminal_state (void)
{
    if (term_needs_restore) {
        /*
         *  Ignore SIGTTOU so we can write to controlling terminal
         *   as background process
         */
        signal (SIGTTOU, SIG_IGN);
        tcsetattr (STDIN_FILENO, TCSADRAIN, &orig_term);
        /* https://en.wikipedia.org/wiki/ANSI_escape_code
         * Best effort: attempt to ensure cursor is visible:
         */
        printf ("\033[?25h\r\n");
        fflush (stdout);
        term_needs_restore = false;
    }
}


static void completion_cb (flux_subprocess_t *p)
{
    struct proxy_command *ctx = flux_subprocess_aux_get (p, "ctx");

    assert (ctx);

    if ((ctx->exit_code = flux_subprocess_exit_code (p)) < 0) {
       /* bash standard, signals + 128 */
       if ((ctx->exit_code = flux_subprocess_signaled (p)) >= 0)
           ctx->exit_code += 128;
    }
    flux_reactor_stop (flux_get_reactor (ctx->h));
    flux_subprocess_destroy (p);
}

static int child_create (struct proxy_command *ctx,
                         int ac,
                         char **av,
                         const char *sockpath)
{
    const char *shell = getenv ("SHELL");
    char *argz = NULL;
    size_t argz_len = 0;
    flux_subprocess_t *p = NULL;
    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_state_change = NULL,
        .on_channel_out = NULL,
        .on_stdout = NULL,
        .on_stderr = NULL,
    };
    flux_cmd_t *cmd = NULL;
    int i;

    if (!shell)
        shell = "/bin/sh";

    if (!(cmd = flux_cmd_create (0, NULL, environ)))
        goto error;

    if (flux_cmd_argv_append (cmd, shell) < 0)
        goto error;

    for (i = 0; i < ac; i++) {
        if (argz_add (&argz, &argz_len, av[i]) != 0) {
            errno = ENOMEM;
            goto error;
        }
    }
    if (argz) {
        /* must use argz_stringify and not loop through argz, want
         * single string passed to -c
         */
        argz_stringify (argz, argz_len, ' ');

        if (flux_cmd_argv_append (cmd, "-c") < 0)
            goto error;

        if (flux_cmd_argv_append (cmd, argz) < 0)
            goto error;
    }

    if (flux_cmd_setenvf (cmd, 1, "FLUX_URI", "local://%s", sockpath) < 0)
        goto error;

    /* We want stdio fallthrough so subprocess can capture tty if
     * necessary (i.e. an interactive shell)
     */
    if (!(p = flux_local_exec (flux_get_reactor (ctx->h),
                               FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH,
                               cmd,
                               &ops,
                               NULL)))
        goto error;

    if (flux_subprocess_aux_set (p, "ctx", ctx, NULL) < 0)
        goto error;

    flux_cmd_destroy (cmd);

    ctx->p = p;
    return 0;
error:
    if (p)
        flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
    return -1;
}

/* Usock client encouters an error.
 */
static void uconn_error (struct usock_conn *uconn, int errnum, void *arg)
{
    struct proxy_command *ctx = arg;

    if (errnum != EPIPE && errnum != EPROTO && errnum != ECONNRESET) {
        const struct flux_msg_cred *cred = usock_conn_get_cred (uconn);
        errno = errnum;
        flux_log_error (ctx->h,
                        "client=%.5s userid=%u",
                        usock_conn_get_uuid (uconn),
                        (unsigned int)cred->userid);
    }
    usock_conn_destroy (uconn);
}

/* Usock client sends message to router.
 */
static void uconn_recv (struct usock_conn *uconn, flux_msg_t *msg, void *arg)
{
    struct router_entry *entry = usock_conn_aux_get (uconn, route_auxkey);

    router_entry_recv (entry, msg);
}


/* Router sends message to a usock client.
 * If event is private, ensure user's credentials allow delivery.
 */
static int uconn_send (const flux_msg_t *msg, void *arg)
{
    struct usock_conn *uconn = arg;
    const struct flux_msg_cred *cred;
    int type;

    if (flux_msg_get_type (msg, &type) < 0)
        return -1;
    switch (type) {
        case FLUX_MSGTYPE_EVENT:
            cred = usock_conn_get_cred (uconn);
            if (auth_check_event_privacy (msg, cred) < 0)
                return -1;
            break;
        default:
            break;
    }
    return usock_conn_send (uconn, msg);
}

/* Accept a connection from new client.
 * This function must call usock_conn_accept() or usock_conn_reject().
 */
static void acceptor_cb (struct usock_conn *uconn, void *arg)
{
    struct proxy_command *ctx = arg;
    const struct flux_msg_cred *cred;
    struct router_entry *entry;

    /* Userid is the user running flux-proxy (else reject).
     * Rolemask in FLUX_ROLE_NONE (delegate to upstream).
     */
    cred = usock_conn_get_cred (uconn);
    if (cred->userid != ctx->proxy_user) {
        errno = EPERM;
        goto error;
    }
    if (!(entry = router_entry_add (ctx->router,
                                    usock_conn_get_uuid (uconn),
                                    uconn_send,
                                    uconn)))
        goto error;
    if (usock_conn_aux_set (uconn,
                            route_auxkey,
                            entry,
                            (flux_free_f)router_entry_delete) < 0) {
        router_entry_delete (entry);
        goto error;
    }
    usock_conn_set_error_cb (uconn, uconn_error, ctx);
    usock_conn_set_recv_cb (uconn, uconn_recv, ctx);
    usock_conn_accept (uconn, cred);
    return;
error:
    usock_conn_reject (uconn, errno);
    usock_conn_destroy (uconn);
}

/* Compare proxy version with broker version.
 * Require major, minor, and patch numbers to match, ignoring patch suffix.
 */
static void version_check (flux_t *h, bool force)
{
    unsigned int n[3];
    const char *version;

    if (!(version = flux_attr_get (h, "version")))
        log_err_exit ("flux_attr_get version");
    if (sscanf (version, "%u.%u.%u", &n[0], &n[1], &n[2]) != 3
        || n[0] != FLUX_CORE_VERSION_MAJOR
        || n[1] != FLUX_CORE_VERSION_MINOR
        || n[2] != FLUX_CORE_VERSION_PATCH) {
        if (force) {
            log_msg ("warning: proxy version %s != broker version %s",
                      FLUX_CORE_VERSION_STRING,
                      version);
        }
        else {
            log_msg_exit ("fatal: proxy version %s != broker version %s "
                          "(--force to connect anyway)",
                          FLUX_CORE_VERSION_STRING,
                          version);
        }
    }
}

static void proxy_command_destroy_usock_and_router (struct proxy_command *ctx)
{
    usock_server_destroy (ctx->server); // destroy before router
    ctx->server = NULL;
    router_destroy (ctx->router);
    ctx->router = NULL;
}

static int cmd_proxy (optparse_t *p, int ac, char *av[])
{
    int n;
    struct proxy_command ctx;
    const char *tmpdir = getenv ("TMPDIR");
    char workpath[PATH_MAX + 1];
    char sockpath[PATH_MAX + 1];
    const char *target;
    char *uri;
    int optindex;
    flux_reactor_t *r;

    log_init ("flux-proxy");

    optindex = optparse_option_index (p);
    if (optindex == ac)
        optparse_fatal_usage (p, 1, "URI argument is required\n");

    target = av[optindex++];
    if (!(uri = uri_resolve (target)))
        log_msg_exit ("Unable to resolve %s to a URI", target);

    memset (&ctx, 0, sizeof (ctx));
    if (!(ctx.h = flux_open (uri, 0)))
        log_err_exit ("%s", uri);
    free (uri);
    flux_log_set_appname (ctx.h, "proxy");
    ctx.proxy_user = getuid ();
    if (!(r = flux_reactor_create (FLUX_REACTOR_SIGCHLD)))
        log_err_exit ("flux_reactor_create");
    if (flux_set_reactor (ctx.h, r) < 0)
        log_err_exit ("flux_set_reactor");

    /* Check proxy version vs broker version
     */
    version_check (ctx.h, optparse_hasopt (p, "force"));

    /* Create router
     */
    if (!(ctx.router = router_create (ctx.h)))
        log_err_exit ("router_create");

    /* Create socket directory.
     */
    n = snprintf (workpath, sizeof (workpath), "%s/flux-proxy-XXXXXX",
                             tmpdir ? tmpdir : "/tmp");
    assert (n < sizeof (workpath));
    if (!mkdtemp (workpath))
        log_err_exit ("error creating proxy socket directory");
    cleanup_push_string(cleanup_directory, workpath);

    n = snprintf (sockpath, sizeof (sockpath), "%s/local", workpath);
    assert (n < sizeof (sockpath));

    /* Create listen socket and watcher to handle new connections
     */
    if (!(ctx.server = usock_server_create (r, sockpath, 0777)))
        log_err_exit ("%s: cannot set up socket listener", sockpath);
    cleanup_push_string (cleanup_file, sockpath);
    usock_server_set_acceptor (ctx.server, acceptor_cb, &ctx);

    /* Create child
     */
    if (child_create (&ctx, ac - optindex, av + optindex, sockpath) < 0)
        log_err_exit ("child_create");

    /* Start reactor
     */
    save_terminal_state ();
    if (flux_reactor_run (r, 0) < 0) {
        if (errno == ECONNRESET)
            log_msg ("Lost connection to Flux");
        else
            log_err ("flux_reactor_run");
        if (!optparse_hasopt (p, "nohup")) {
            log_msg ("Sending SIGHUP to child processes");
            flux_future_destroy (flux_subprocess_kill (ctx.p, SIGHUP));
            flux_future_destroy (flux_subprocess_kill (ctx.p, SIGCONT));
        }
        proxy_command_destroy_usock_and_router (&ctx);

        /*
         * Wait for child to normally terminate
         */
        flux_reactor_run (r, 0);
        restore_terminal_state ();
        goto done;
    }
done:
    proxy_command_destroy_usock_and_router (&ctx);

    if (ctx.exit_code)
        exit (ctx.exit_code);

    flux_close (ctx.h);
    return (0);
}

static struct optparse_option proxy_opts[] = {
    { .name = "force",  .key = 'f',  .has_arg = 0,
      .usage = "Skip version check when connecting to Flux broker", },
    { .name = "nohup",  .key = 'n',  .has_arg = 0,
      .usage = "Do not send SIGHUP to child processes when connection"
               " to Flux is lost", },
    OPTPARSE_TABLE_END,
};

int subcommand_proxy_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p, "proxy", cmd_proxy,
        "[OPTIONS] JOBID|URI [COMMAND...]",
        "Route messages to/from Flux instance",
        0,
        proxy_opts);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
