/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* relay.c - act as message relay for ssh:// connector
 *
 * This is similar to flux-proxy(1) except that instead of spawning
 * children that connect to a locally provided socket, it only handles
 * one client, pre-connected on stdin, stdout.
 *
 * The ssh connector starts flux-relay(1) remotely with ssh.
 * flux-relay(1) connects to a flux broker on the remote system.
 * The ssh connector communicates with flux-relay through stdio.
 *
 * The relay exits once the client closes its handle and the ssh
 * stdio streams are torn down.
 *
 * The ssh URI looks like:
 *   ssh://[user@]hostname[:port]/unix-path
 *
 * Which the ssh connector translates to:
 *   ssh [-p port] [user@]hostname flux-relay /unix-path
 *
 * The flux-relay command flux_open()'s local:///unix-path.
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif
#include "builtin.h"
#include <unistd.h>
#include <sys/types.h>
#include <limits.h>
#include <stdio.h>
#include <glob.h>
#include <ctype.h>

#include "src/common/librouter/router.h"
#include "src/common/librouter/usock.h"
#include "src/common/librouter/auth.h"

/* Usock client encounters an error.
 */
static void uconn_error (struct usock_conn *uconn, int errnum, void *arg)
{
    flux_reactor_t *r = arg;

    /* N.B. Closing our read file descriptor triggers ECONNRESET
     * from flux_msg_recvfd() so suppress logging that one.
     */
    if (errno != ECONNRESET) {
        errno = errnum;
        log_err ("client error");
    }
    flux_reactor_stop (r);
}


/* Usock client sends message to router.
 */
static void uconn_recv (struct usock_conn *uconn, flux_msg_t *msg, void *arg)
{
    struct router_entry *entry = arg;

    router_entry_recv (entry, msg);
}


/* Router sends message to a usock client.
 * No need to check whether client is allowed to receive an event,
 * since client has same creds as relay process.
 */
static int uconn_send (const flux_msg_t *msg, void *arg)
{
    struct usock_conn *uconn = arg;

    return usock_conn_send (uconn, msg);
}

static void relay (int infd, int outfd, flux_t *h)
{
    struct router *router;
    struct router_entry *entry;
    struct usock_conn *uconn;
    struct flux_msg_cred cred;
    flux_reactor_t *r;

    if (!(r = flux_get_reactor (h)))
        log_err_exit ("flux_get_reactor");

    if (!(router = router_create (h)))
        log_err_exit ("router_create");

    if (!(uconn = usock_conn_create (r, infd, outfd)))
        log_err_exit ("usock_conn_create");

    if (!(entry = router_entry_add (router,
                                    usock_conn_get_uuid (uconn),
                                    uconn_send,
                                    uconn)))
        log_err_exit ("router_entry_add");

    usock_conn_set_error_cb (uconn, uconn_error, r);
    usock_conn_set_recv_cb (uconn, uconn_recv, entry);

    /* Use uid of the relay process as the userid for the
     * single "client" on stdin.
     */
    cred.userid = getuid ();
    cred.rolemask = FLUX_ROLE_NONE; // delegate to "upstream"
    usock_conn_accept (uconn, &cred);

    /* Reactor runs until uconn_error() is called upon client disconnect.
     */
    if (flux_reactor_run (r, 0) < 0)
        log_err_exit ("flux_reactor_run");

    router_entry_delete (entry);
    usock_conn_destroy (uconn);
    router_destroy (router);
}

static int cmd_relay (optparse_t *p, int ac, char *av[])
{
    flux_t *h;
    int optindex;
    char *uri;
    char hostname [_POSIX_HOST_NAME_MAX + 1];

    /*  If possible, initialize logging prefix as local hostname. (In the
     *  unlikely event gethostname(3) fails, use "unknown-host".)
     *
     *  This will be more helpful than a literal "flux-relay" logging prefix
     *  for end users that may be unknowingly using flux-relay as part of
     *  the ssh connector.
     */
    log_init ("unknown-host");
    if (gethostname (hostname, sizeof (hostname)) == 0)
        log_init (hostname);

    optindex = optparse_option_index (p);
    if (optindex == ac)
        optparse_fatal_usage (p, 1, "path argument is required\n");
    if (asprintf (&uri, "local://%s", av[optindex++]) < 0)
        log_err_exit ("asprintf");

    if (optindex != ac) {
        optparse_print_usage (p);
        exit (1);
    }

    if (!(h = flux_open (uri, 0)))
        log_err_exit ("%s", uri);

    flux_log_set_appname (h, "relay");

    relay (STDIN_FILENO, STDOUT_FILENO, h);

    flux_close (h);
    free (uri);
    return (0);
}

int subcommand_relay_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p, "relay", cmd_relay,
        "[OPTIONS] path",
        "Relay messages between stdio and local://path",
        0,
        NULL);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
