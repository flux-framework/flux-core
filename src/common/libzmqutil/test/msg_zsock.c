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
#include <stdbool.h>
#include <czmq.h>
#include <errno.h>
#include <stdio.h>

#include "src/common/libflux/message.h"
#include "src/common/libzmqutil/msg_zsock.h"
#include "src/common/libtap/tap.h"

void check_sendzsock (void)
{
    zsock_t *zsock[2] = { NULL, NULL };
    flux_msg_t *any, *msg, *msg2;
    const char *topic;
    int type;
    const char *uri = "inproc://test";

    /* zsys boiler plate:
     * appears to be needed to avoid atexit assertions when lives_ok()
     * macro (which calls fork()) is used.
     */
    zsys_init ();
    zsys_set_logstream (stderr);
    zsys_set_logident ("test_message.t");
    zsys_handler_set (NULL);
    zsys_set_linger (5); // msec

    ok ((zsock[0] = zsock_new_pair (NULL)) != NULL
                    && zsock_bind (zsock[0], "%s", uri) == 0
                    && (zsock[1] = zsock_new_pair (uri)) != NULL,
        "got inproc socket pair");

    if (!(any = flux_msg_create (FLUX_MSGTYPE_ANY)))
        BAIL_OUT ("flux_msg_create failed");

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL
            && flux_msg_set_topic (msg, "foo.bar") == 0,
        "created test message");

    /* corner case tests */
    ok (zmqutil_msg_send (NULL, msg) < 0 && errno == EINVAL,
        "zmqutil_msg_send returns < 0 and EINVAL on dest = NULL");
    ok (zmqutil_msg_send (zsock[1], any) < 0 && errno == EPROTO,
        "zmqutil_msg_send returns < 0 and EPROTO on msg w/ type = ANY");
    ok (zmqutil_msg_send_ex (NULL, msg, true) < 0 && errno == EINVAL,
        "zmqutil_msg_send_ex returns < 0 and EINVAL on dest = NULL");
    ok (zmqutil_msg_send_ex (zsock[1], any, true) < 0 && errno == EPROTO,
        "zmqutil_msg_send_ex returns < 0 and EPROTO on msg w/ type = ANY");
    ok (zmqutil_msg_recv (NULL) == NULL && errno == EINVAL,
        "zmqutil_msg_recv returns NULL and EINVAL on dest = NULL");

    ok (zmqutil_msg_send (zsock[1], msg) == 0,
        "zmqutil_msg_send works");
    ok ((msg2 = zmqutil_msg_recv (zsock[0])) != NULL,
        "zmqutil_msg_recv works");
    ok (flux_msg_get_type (msg2, &type) == 0 && type == FLUX_MSGTYPE_REQUEST
            && flux_msg_get_topic (msg2, &topic) == 0
            && streq (topic, "foo.bar")
            && flux_msg_has_payload (msg2) == false,
        "decoded message looks like what was sent");
    flux_msg_destroy (msg2);

    /* Send it again.
     */
    ok (zmqutil_msg_send (zsock[1], msg) == 0,
        "try2: zmqutil_msg_send works");
    ok ((msg2 = zmqutil_msg_recv (zsock[0])) != NULL,
        "try2: zmqutil_msg_recv works");
    ok (flux_msg_get_type (msg2, &type) == 0 && type == FLUX_MSGTYPE_REQUEST
            && flux_msg_get_topic (msg2, &topic) == 0
            && streq (topic, "foo.bar")
            && flux_msg_has_payload (msg2) == false,
        "try2: decoded message looks like what was sent");
    flux_msg_destroy (msg2);
    flux_msg_destroy (msg);

    zsock_destroy (&zsock[0]);
    zsock_destroy (&zsock[1]);

    /* zsys boiler plate - see note above
     */
    zsys_shutdown();
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    check_sendzsock ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

