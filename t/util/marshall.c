/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* marshall.c - encode/decode Flux messages on stdout/stdin */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdio.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/librouter/sendfd.h"
#include "ccan/str/str.h"

void check_msg_cred (const flux_msg_t *msg,
                     const char *type,
                     const struct flux_msg_cred want)
{
    struct flux_msg_cred got;

    if (flux_msg_get_cred (msg, &got) < 0)
        log_err_exit ("error decoding %s cred", type);
    if (want.userid != got.userid) {
        log_msg_exit ("%s userid: expected %lu got %lu",
                      type,
                      (unsigned long)want.userid,
                      (unsigned long)got.userid);
    }
    if (want.rolemask != got.rolemask) {
        log_msg_exit ("%s rolemask: expected 0x%lx got 0x%lx",
                      type,
                      (unsigned long)want.rolemask,
                      (unsigned long)got.rolemask);
    }
}

void check_topic (const flux_msg_t *msg,
                  const char *type,
                  const char *topic)
{
    const char *t;

    if (flux_msg_get_topic (msg, &t) < 0)
        log_err_exit ("error decoding %s topic", type);
    if (!streq (t, topic)) {
        log_msg_exit ("%s topic: expected %s got %s", type, topic, t);
    }
}

void check_matchtag (const flux_msg_t *msg,
                     const char *type,
                     uint32_t matchtag)
{
    uint32_t m;

    if (flux_msg_get_matchtag (msg, &m) < 0)
        log_err_exit ("error decoding %s matchtag", type);
    if (matchtag != m) {
        log_msg_exit ("%s matchtag: expected %lu got %lu",
                      type,
                      (unsigned long)matchtag,
                      (unsigned long)m);
    }
}

void check_payload (const flux_msg_t *msg,
                    const char *type,
                    const char *payload)
{
    const char *s = NULL;

    if (flux_msg_get_string (msg, &s) < 0)
        log_err_exit ("error decoding %s payload", type);
    if (payload) {
        if (!s)
            log_msg_exit ("%s payload: expected %s got NULL", type, payload);
        if (!streq (s, payload))
            log_msg_exit ("%s payload: expected %s got %s", type, payload, s);
    }
    else if (s)
        log_msg_exit ("%s payload: expected NULL got %s", type, s);
}

void check_errnum (const flux_msg_t *msg,
                   const char *type,
                   int errnum)
{
    int n;
    if (flux_msg_get_errnum (msg, &n) < 0)
        log_err_exit ("error decoding %s errnum", type);
    if (errnum != n)
        log_msg_exit ("%s errnum: expected %d got %d", type, errnum, n);
}

void check_type (const flux_msg_t *msg,
                 const char *typename,
                 int type)
{
    int n;
    if (flux_msg_get_type (msg, &n) < 0)
        log_err_exit ("error decoding %s type", typename);
    if (type != n)
        log_msg_exit ("%s type: expected %d got %d", typename, type, n);
}

void check_control (const flux_msg_t *msg,
                    const char *typestr,
                    int type,
                    int status)
{
    int t, s;
    if (flux_control_decode (msg, &t, &s) < 0)
        log_err_exit ("error decoding %s", typestr);
    if (type != t)
        log_msg_exit ("%s type: expected %d got %d", typestr, type, t);
    if (status != s)
        log_msg_exit ("%s status: expected %d got %d", typestr, status, s);
}

void check_route_count (const flux_msg_t *msg,
                        const char *type,
                        int count)
{
    int n;
    n = flux_msg_route_count (msg);
    if (n != count)
        log_msg_exit ("%s route count: expected %d got %d", type, count, n);
}

int main (int argc, char *argv[])
{
    struct flux_msg_cred xcred = {
        .userid = 1234,
        .rolemask = FLUX_ROLE_OWNER
    };
    struct flux_msg_cred ucred = {
        .userid = FLUX_USERID_UNKNOWN,
        .rolemask = FLUX_ROLE_NONE,
    };

    log_init ("marshall");

    if (argc == 2 && streq (argv[1], "encode")) {
        flux_msg_t *msg;
        flux_msg_t *msg2;

        /* request */
        if (!(msg = flux_request_encode ("sample.topic", "payload"))
            || flux_msg_set_cred (msg, xcred) < 0
            || flux_msg_set_matchtag (msg, 42) < 0)
            log_err_exit ("error encoding request");
        flux_msg_route_enable (msg);
        if (flux_msg_route_push (msg, "route1") < 0)
            log_err_exit ("error adding route to request");
        if (sendfd (STDOUT_FILENO, msg, NULL) < 0)
            log_err_exit ("sendfd");

        /* error response */
        if (!(msg2 = flux_response_derive (msg, EINVAL)))
            log_err_exit ("error encoding response");
        if (sendfd (STDOUT_FILENO, msg2, NULL) < 0)
            log_err_exit ("sendfd");
        flux_msg_destroy (msg2);

        /* normal response */
        if (!(msg2 = flux_response_derive (msg, 0))
            || flux_msg_set_string (msg2, "return-payload") < 0)
            log_err_exit ("error encoding response");
        if (sendfd (STDOUT_FILENO, msg2, NULL) < 0)
            log_err_exit ("sendfd");
        flux_msg_destroy (msg2);

        flux_msg_destroy (msg);

        /* event */
        if (!(msg = flux_event_encode ("sample.topic", NULL))
            || flux_msg_set_cred (msg, xcred) < 0
            || flux_msg_set_private (msg))
            log_err_exit ("error encoding event");
        if (sendfd (STDOUT_FILENO, msg, NULL) < 0)
            log_err_exit ("sendfd");
        flux_msg_destroy (msg);

        /* control */
        if (!(msg = flux_control_encode (0x0a0b0c0d, 0x00010203)))
            log_err_exit ("error encoding control message");
        if (sendfd (STDOUT_FILENO, msg, NULL) < 0)
            log_err_exit ("sendfd");
        flux_msg_destroy (msg);

    }
    else if (argc == 2 && streq (argv[1], "decode")) {
        flux_msg_t *msg;

        /* request */
        if (!(msg = recvfd (STDIN_FILENO, NULL)))
            log_err_exit ("recvfd");
        check_type (msg, "request", FLUX_MSGTYPE_REQUEST);
        check_matchtag (msg, "request", 42);
        check_msg_cred (msg, "request", xcred);
        check_topic (msg, "request", "sample.topic");
        check_payload (msg, "request", "payload");
        check_route_count (msg, "request", 1);
        flux_msg_destroy (msg);

        /* error response */
        if (!(msg = recvfd (STDIN_FILENO, NULL)))
            log_err_exit ("recvfd");
        check_type (msg, "error response", FLUX_MSGTYPE_RESPONSE);
        check_matchtag (msg, "error response", 42);
        check_msg_cred (msg, "error response", ucred);
        check_topic (msg, "error response", "sample.topic");
        check_payload (msg, "error response", NULL);
        check_errnum (msg, "error response", EINVAL);
        check_route_count (msg, "error response", 1);
        flux_msg_destroy (msg);

        /* normal response */
        if (!(msg = recvfd (STDIN_FILENO, NULL)))
            log_err_exit ("recvfd");
        check_type (msg, "normal response", FLUX_MSGTYPE_RESPONSE);
        check_matchtag (msg, "normal response", 42);
        check_msg_cred (msg, "normal response", ucred);
        check_topic (msg, "normal response", "sample.topic");
        check_payload (msg, "normal response", "return-payload");
        check_errnum (msg, "normal response", 0);
        check_route_count (msg, "normal response", 1);
        flux_msg_destroy (msg);

        /* event */
        if (!(msg = recvfd (STDIN_FILENO, NULL)))
            log_err_exit ("recvfd");
        check_type (msg, "event", FLUX_MSGTYPE_EVENT);
        check_msg_cred (msg, "event", xcred);
        check_topic (msg, "event", "sample.topic");
        check_payload (msg, "event", NULL);
        if (!flux_msg_is_private (msg))
            log_err_exit ("event: expected private got non-private");
        flux_msg_destroy (msg);

        /* control*/
        if (!(msg = recvfd (STDIN_FILENO, NULL)))
            log_err_exit ("recvfd");
        check_type (msg, "control", FLUX_MSGTYPE_CONTROL);
        check_control (msg, "control", 0x0a0b0c0d, 0x00010203);
        flux_msg_destroy (msg);
    }
    else
        fprintf (stderr, "Usage: marshall encode|decode\n");

    return 0;
}

// vi:ts=4 sw=4 expandtab
