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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <flux/core.h>

#include "src/common/librouter/sendfd.h"
#include "src/common/libtap/tap.h"

/* Send a small message over a blocking pipe.
 * We assume that there's enough buffer to do this in one go.
 */
void check_sendfd (void)
{
    int pfd[2];
    flux_msg_t *msg, *msg2;
    const char *topic;
    int type;

    ok (pipe2 (pfd, O_CLOEXEC) == 0,
        "got blocking pipe");
    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "flux_msg_create works");
    ok (flux_msg_set_topic (msg, "foo.bar") == 0,
        "flux_msg_set_topic works");
    ok (sendfd (pfd[1], msg, NULL) == 0,
        "sendfd works");
    ok ((msg2 = recvfd (pfd[0], NULL)) != NULL,
        "recvfd works");
    ok (flux_msg_get_type (msg2, &type) == 0 && type == FLUX_MSGTYPE_REQUEST,
        "decoded expected message type");
    ok (flux_msg_get_topic (msg2, &topic) == 0 && !strcmp (topic, "foo.bar"),
        "decoded expected topic string");
    ok (flux_msg_has_payload (msg2) == false,
        "decoded expected (lack of) payload");

    flux_msg_destroy (msg);
    flux_msg_destroy (msg2);
    close (pfd[1]);
    close (pfd[0]);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    check_sendfd ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

