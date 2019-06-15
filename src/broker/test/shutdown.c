/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <flux/core.h>
#include <czmq.h>
#include <stdio.h>

#include "shutdown.h"

#include "src/common/libtap/tap.h"

void fatal_err (const char *message, void *arg)
{
    BAIL_OUT ("fatal error: %s", message);
}

void shutdown_cb (shutdown_t *s, bool expired, void *arg)
{
    int rc = shutdown_get_rc (s);

    ok (rc == 42,
        "shutowon callback retrieved exitcode");
}

void log_request_cb (flux_t *h, flux_msg_handler_t *w,
                     const flux_msg_t *msg, void *arg)
{
    ok (msg != NULL,
        "shutdown log message from rank 0 received");
    flux_msg_handler_stop (w);
}

flux_msg_t *shutdown_encode (double grace, int exitcode, int rank,
                             const char *fmt, ...)
{
    va_list ap;
    flux_msg_t *msg;

    va_start (ap, fmt);
    msg = shutdown_vencode (grace, exitcode, rank, fmt, ap);
    va_end (ap);

    return msg;
}

void check_codec (void)
{
    flux_msg_t *msg;
    char r[256];
    int rank, exitcode;
    double grace;

    ok ((msg = shutdown_encode (3.14, 69, 41, "%s", "foo")) != NULL,
        "shutdown_encode works");
    ok (shutdown_decode (msg, &grace, &exitcode, &rank, r, sizeof (r)) == 0
        && grace == 3.14 && exitcode == 69 && rank ==41 
        && !strcmp (r, "foo"),
        "shutdown_decode works");

    flux_msg_destroy (msg);
}

int main (int argc, char **argv)
{
    flux_t *h;
    shutdown_t *sh;
    flux_msg_handler_t *log_w;
    struct flux_match matchlog = FLUX_MATCH_REQUEST;

    plan (11);

    check_codec ();

    (void)setenv ("FLUX_CONNECTOR_PATH",
                  flux_conf_get ("connector_path", CONF_FLAG_INTREE), 0);
    ok ((h = flux_open ("loop://", 0)) != NULL,
        "opened loop connector");
    if (!h)
        BAIL_OUT ("can't continue without loop handle");
    flux_fatal_set (h, fatal_err, NULL);

    ok ((sh = shutdown_create ()) != NULL,
        "shutdown_create works");
    shutdown_set_flux (sh, h);
    shutdown_set_grace (sh, 0.1);
    shutdown_set_callback (sh, shutdown_cb, NULL);

    matchlog.topic_glob = "log.append";
    log_w = flux_msg_handler_create (h, matchlog, log_request_cb, sh);
    ok (log_w != NULL,
        "created log.append watcher");
    flux_msg_handler_start (log_w);

    ok (shutdown_arm (sh, 42, "testing %d %d %d", 1, 2, 3) == 0,
        "shutdown event sent, starting reactor");
    ok (flux_reactor_run (flux_get_reactor (h), 0) == 0,
        "flux reactor exited normally");

    /* Make sure shutdown_disarm unwires timer.
     * (other watchers have already stopped themselves above)
     */
    ok (shutdown_arm (sh, 42, "testing %d %d %d", 1, 2, 3) == 0,
        "shutdown event sent, then disarmed, starting reactor");
    shutdown_disarm (sh);
    ok (flux_reactor_run (flux_get_reactor (h), 0) == 0,
        "flux reactor exited normally");

    shutdown_destroy (sh);
    flux_msg_handler_destroy (log_w);
    flux_close (h);

    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
