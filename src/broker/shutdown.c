/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* shutdown.c - manage instance shutdown on behalf of flux-shutdown(1)
 *
 * This is only active on rank 0.
 * On rank 0, this posts the "goodbye" event to the broker state machine.
 * On other ranks it is generated internally in state_machine.c.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libutil/stdlog.h"

#include "shutdown.h"
#include "state_machine.h"

#include "broker.h"

struct shutdown {
    struct broker *ctx;
    flux_msg_handler_t **handlers;

    flux_future_t *f_monitor;
    broker_state_t state;

    flux_future_t *f_dmesg;

    const flux_msg_t *request; // single flux-shutdown(1) client
};

static void dmesg_cancel (flux_future_t *f);

static void check_for_completion (struct shutdown *shutdown)
{
    flux_t *h = shutdown->ctx->h;

    if (shutdown->state != STATE_GOODBYE)
        return;
    if (shutdown->f_dmesg) {
        dmesg_cancel (shutdown->f_dmesg);
        return;
    }
    if (shutdown->request) {
        if (flux_respond_error (h, shutdown->request, ENODATA, NULL) < 0)
            flux_log_error (h, "error responding to shutdown.start");
        return;
    }
    state_machine_post (shutdown->ctx->state_machine, "goodbye");
}

static int forward_logbuf (flux_t *h,
                           const flux_msg_t *request,
                           const char *stdlog)
{
    struct stdlog_header hdr;
    const char *txt;
    int txtlen;
    char buf[FLUX_MAX_LOGBUF];
    int loglevel;

    if (flux_msg_unpack (request, "{s:i}", "loglevel", &loglevel) < 0)
        loglevel = LOG_ERR;

    if (stdlog_decode (stdlog,
                       strlen (stdlog),
                       &hdr,
                       NULL,
                       NULL,
                       &txt,
                       &txtlen) < 0
        || STDLOG_SEVERITY (hdr.pri) > loglevel
        || snprintf (buf,
                     sizeof (buf),
                     "%s.%s[%lu]: %.*s\n",
                     hdr.appname,
                     stdlog_severity_to_string (STDLOG_SEVERITY (hdr.pri)),
                     strtoul (hdr.hostname, NULL, 10),
                     txtlen,
                     txt) >= sizeof (buf))
        return 0;
    return flux_respond_pack (h, request, "{s:s}", "log", buf);
}

static void dmesg_continuation (flux_future_t *f, void *arg)
{
    struct shutdown *shutdown = arg;
    flux_t *h = flux_future_get_flux (f);
    const char *buf;

    if (flux_rpc_get (f, &buf) < 0) {
        if (errno != ENODATA)
            flux_log_error (h, "shutdown: log.dmesg");
        flux_future_destroy (f);
        shutdown->f_dmesg = NULL;
        check_for_completion (shutdown);
        return;
    }
    if (shutdown->request) {
        if (forward_logbuf (h, shutdown->request, buf) < 0)
            flux_log_error (h, "error responding to shutdown.start");
    }
    flux_future_reset (f);
}

static void dmesg_cancel (flux_future_t *f)
{
    flux_t *h = flux_future_get_flux (f);
    uint32_t matchtag = flux_rpc_get_matchtag (f);
    flux_future_t *f_cancel;

    if (!(f_cancel = flux_rpc_pack (h,
                                    "log.cancel",
                                    FLUX_NODEID_ANY,
                                    FLUX_RPC_NORESPONSE,
                                    "{s:i}",
                                    "matchtag", matchtag)))
        flux_log_error (h, "shutdown: error sending dmesg.cancel RPC");
    flux_future_destroy (f_cancel);
}

static flux_future_t *dmesg_request (struct shutdown *shutdown)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (shutdown->ctx->h,
                             "log.dmesg",
                             FLUX_NODEID_ANY,
                             FLUX_RPC_STREAMING,
                             "{s:b s:b}",
                             "follow", 1,
                             "nobacklog", 1))
        || flux_future_then (f,
                             -1,
                             dmesg_continuation,
                             shutdown) < 0) {
        flux_future_destroy (f);
        return NULL;
    }
    return f;
}

static void start_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    struct shutdown *shutdown = arg;
    flux_error_t error;
    const char *errmsg = NULL;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (shutdown->request) {
        errno = EINVAL;
        errmsg = "shutdown is already in progress";
        goto error;
    }
    if (state_machine_shutdown (shutdown->ctx->state_machine, &error) < 0) {
        errmsg = error.text;
        goto error;
    }
    if (flux_msg_is_streaming (msg)) {
        if (!(shutdown->f_dmesg)) {
            if (!(shutdown->f_dmesg = dmesg_request (shutdown))) {
                errmsg = "error requesting to follow log messages";
                goto error;
            }
        }
        shutdown->request = flux_msg_incref (msg);
    }
    else {
        if (flux_respond (h, msg, NULL) < 0)
            flux_log_error (h, "error responding to shutdown.start");
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to shutdown.start");
}

static void monitor_continuation (flux_future_t *f, void *arg)
{
    flux_t *h = flux_future_get_flux (f);
    struct shutdown *shutdown = arg;

    if (flux_rpc_get_unpack (f, "{s:i}", "state", &shutdown->state) < 0) {
        if (errno != ENODATA)
            flux_log_error (h, "shutdown: state-machine.monitor");
        flux_future_destroy (f);
        shutdown->f_monitor = NULL;
        check_for_completion (shutdown);
        return;
    }
    flux_future_reset (f);
}

static flux_future_t *monitor_request (struct shutdown *shutdown)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (shutdown->ctx->h,
                             "state-machine.monitor",
                             FLUX_NODEID_ANY,
                             FLUX_RPC_STREAMING,
                             "{s:i}",
                             "final", STATE_GOODBYE))
        || flux_future_then (f, -1, monitor_continuation, shutdown) < 0) {
        flux_future_destroy (f);
        return NULL;
    }
    return f;
}

static void disconnect_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct shutdown *shutdown = arg;

    if (shutdown->request && flux_disconnect_match (msg, shutdown->request)) {
        flux_msg_decref (shutdown->request);
        shutdown->request = NULL;
        check_for_completion (shutdown);
    }
}

static const struct flux_msg_handler_spec htab[] = {
    {   FLUX_MSGTYPE_REQUEST,
        "shutdown.disconnect",
        disconnect_cb,
        0
    },
    {    FLUX_MSGTYPE_REQUEST,
        "shutdown.start",
        start_cb,
        0,
    },
    FLUX_MSGHANDLER_TABLE_END,
};

void shutdown_destroy (struct shutdown *shutdown)
{
    if (shutdown) {
        int saved_errno = errno;
        flux_msg_decref (shutdown->request);
        flux_msg_handler_delvec (shutdown->handlers);
        flux_future_destroy (shutdown->f_dmesg);
        flux_future_destroy (shutdown->f_monitor);
        free (shutdown);
        errno = saved_errno;
    }
}

struct shutdown *shutdown_create (struct broker *ctx)
{
    struct shutdown *shutdown;

    if (!(shutdown = calloc (1, sizeof (*shutdown))))
        return NULL;
    shutdown->ctx = ctx;
    if (flux_msg_handler_addvec (ctx->h,
                                    htab,
                                    shutdown,
                                    &shutdown->handlers) < 0)
        goto error;
    if (ctx->rank == 0) {
        if (!(shutdown->f_monitor = monitor_request (shutdown)))
            return NULL;
    }
    return shutdown;
error:
    shutdown_destroy (shutdown);
    return NULL;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
