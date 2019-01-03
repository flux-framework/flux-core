/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <errno.h>
#include <string.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "util.h"

int main (int argc, char *argv[])
{
    flux_t *h;
    flux_msg_t *msg;
    const char *topic;
    const char *reason;
    int flags;

    plan (NO_PLAN);

    if (!(h = loopback_create (0)))
        BAIL_OUT ("loopback_create failed");

    /* Send request
     */
    ok (flux_panic (h, 0, 0, "fubar") == 0,
        "flux_panic works");

    /* Receive request on the loopback
     */
    msg = flux_recv (h, FLUX_MATCH_ANY, 0);
    ok (msg != NULL,
        "flux_recv received message on loop");
    ok (flux_request_unpack (msg, &topic, "{s:s s:i}",
                             "reason", &reason, "flags", &flags) == 0,
        "flux_request_unpack worked on panic request");
    ok (topic != NULL && !strcmp (topic, "cmb.panic"),
        "topic string is correct");
    ok (!strcmp (reason, "fubar"),
        "reason is correct");
    ok (flags == 0,
        "flags is correct");
    flux_msg_destroy (msg);

    /* invalid arguments
     */
    errno = 0;
    ok (flux_panic (NULL, 0, 0, "foo") < 0 && errno == EINVAL,
        "flux_panic h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_panic (h, 0, 1, "foo") < 0 && errno == EINVAL,
        "flux_panic flags=1 fails with EINVAL");
    errno = 0;
    ok (flux_panic (h, 0, 0, NULL) < 0 && errno == EINVAL,
        "flux_panic reason=NULL fails with EINVAL");

    flux_close (h);

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

