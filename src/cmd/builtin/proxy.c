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
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <stdbool.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif
#include <glob.h>
#include <inttypes.h>
#include <termios.h>

#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/uri.h"
#include "src/common/librouter/usock.h"
#include "src/common/librouter/router.h"

extern char **environ;

struct proxy_command {
    struct usock_server *server;
    struct router *router;
    flux_t *h;
    flux_subprocess_t *p;
    int exit_code;
    uid_t proxy_user;
    char *remote_uri_authority;
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
    int flags = 0;
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
    if (ctx->remote_uri_authority
        && flux_cmd_setenvf (cmd, 1,
                             "FLUX_PROXY_REMOTE",
                             "%s",
                             ctx->remote_uri_authority) < 0)
        goto error;

    /* We want stdio fallthrough so subprocess can capture tty if
     * necessary (i.e. an interactive shell).
     */
    flags |= FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH;
    flags |= FLUX_SUBPROCESS_FLAGS_NO_SETPGRP;
    if (!(p = flux_local_exec (flux_get_reactor (ctx->h),
                               flags,
                               cmd,
                               &ops)))
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

/* Usock client encounters an error.
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
 * Require major and minor to match.  Ignore patch and any git suffix.
 */
static void version_check (flux_t *h, bool force)
{
    unsigned int n[3];
    const char *version;

    if (!(version = flux_attr_get (h, "version")))
        log_err_exit ("flux_attr_get version");
    if (sscanf (version, "%u.%u.%u", &n[0], &n[1], &n[2]) != 3
        || n[0] != FLUX_CORE_VERSION_MAJOR
        || n[1] != FLUX_CORE_VERSION_MINOR) {
        if (force) {
            log_msg ("warning: proxy version %s may not interoperate"
                     " with broker version %s",
                      FLUX_CORE_VERSION_STRING,
                      version);
        }
        else {
            log_msg_exit ("fatal: proxy version %s may not interoperate"
                          " with broker version %s "
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

/* Attempt to reconnect to broker.  If successful, wait for for broker to
 * reach RUN state to avoid "Upstream broker is offline" errors when connecting
 * early in the broker's startup.  Returns 0 on success, -1 on failure.
 * On failure, it is safe to call this function again to retry.  This function
 * calls exit(1) on errors that are unexpected in the reconnect context.
 *
 * Notes
 * - error callback is protected against reentry
 * - send/recv within the callback can fail as though no callback is registered
 * - state-machine.wait RPC fails if broker is shutting down, otherwise blocks
 * - it is safe to call flux_reconnect(3) if already connected
 * - router_renew() re-establishes client event subs and service regs
 */
static int try_reconnect (flux_t *h, struct router *router)
{
    flux_future_t *f;
    int rc = -1;

    if (flux_reconnect (h) < 0) {
        if (errno == ENOSYS)
            log_msg_exit ("reconnect not implemented by connector");
        return -1;
    }
    if (!(f = flux_rpc (h, "state-machine.wait", NULL, FLUX_NODEID_ANY, 0))
        || flux_rpc_get (f, NULL) < 0) {
        log_msg ("state-machine.wait: %s", future_strerror (f, errno));
        goto done;
    }
    if (router_renew (router) < 0) {
        log_err ("failed to restore subscriptions/service registrations");
        goto done;
    }
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

static int comms_error (flux_t *h, void *arg)
{
    struct proxy_command *ctx = arg;

    log_msg ("broker: %s", strerror (errno));
    log_msg ("reconnecting");
    while (try_reconnect (h, ctx->router) < 0)
        sleep (2);
    log_msg ("reconnected");
    return 0;
}

static int cmd_proxy (optparse_t *p, int ac, char *av[])
{
    struct proxy_command ctx;
    const char *tmpdir = getenv ("TMPDIR");
    char workpath[PATH_MAX + 1];
    char sockpath[PATH_MAX + 1];
    const char *target;
    char *uri;
    int optindex;
    flux_reactor_t *r;
    int flags = 0;

    memset (&ctx, 0, sizeof (ctx));

    log_init ("flux-proxy");

    optindex = optparse_option_index (p);
    if (optindex == ac)
        optparse_fatal_usage (p, 1, "URI argument is required\n");

    target = av[optindex++];
    if (!(uri = uri_resolve (target, NULL)))
        log_msg_exit ("Unable to resolve %s to a URI", target);

    if (optparse_hasopt (p, "reconnect"))
        flags |= FLUX_O_RPCTRACK;

    if (!(ctx.h = flux_open (uri, flags)))
        log_err_exit ("%s", uri);
    ctx.remote_uri_authority = uri_remote_get_authority (uri);
    free (uri);
    flux_log_set_appname (ctx.h, "proxy");
    ctx.proxy_user = getuid ();
    if (!(r = flux_get_reactor (ctx.h)))
        log_err_exit ("flux_get_reactor");

    /* Register handler for loss of broker connection if --reconnect
     */
    if (optparse_hasopt (p, "reconnect"))
        flux_comms_error_set (ctx.h, comms_error, &ctx);

    /* Check proxy version vs broker version
     */
    version_check (ctx.h, optparse_hasopt (p, "force"));

    /* Create router
     */
    if (!(ctx.router = router_create (ctx.h)))
        log_err_exit ("router_create");

    /* Create socket directory.
     */
    if (snprintf (workpath,
                  sizeof (workpath),
                  "%s/flux-proxy-XXXXXX",
                  tmpdir ? tmpdir : "/tmp") >= sizeof (workpath))
        log_msg_exit ("TMPDIR is too long for internal buffer");
    if (!mkdtemp (workpath))
        log_err_exit ("error creating proxy socket directory");
    cleanup_push_string(cleanup_directory, workpath);

    if (snprintf (sockpath,
                  sizeof (sockpath),
                  "%s/local",
                  workpath) >= sizeof (sockpath))
        log_msg_exit ("TMPDIR is too long for internal buffer");

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
    free (ctx.remote_uri_authority);

    if (ctx.exit_code)
        exit (ctx.exit_code);

    flux_close (ctx.h);
    return (0);
}

static struct optparse_option proxy_opts[] = {
    { .name = "force",  .key = 'f',  .has_arg = 0,
      .usage = "Skip checks when connecting to Flux broker", },
    { .name = "nohup",  .key = 'n',  .has_arg = 0,
      .usage = "Do not send SIGHUP to child processes when connection"
               " to Flux is lost", },
    { .name = "reconnect",  .has_arg = 0,
      .usage = "If broker connection is lost, try to reconnect", },
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
