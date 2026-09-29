/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Exercise the multi-rank test server.
 *
 * Each rank answers "echorank" with the rank attribute it was given, so a
 * response proves which instance handled the request.  That is the property
 * the harness exists to provide and the one a caller depends on.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"
#include "src/common/libtestutil/util_multi.h"

#define SIZE 4

static void echorank_cb (flux_t *h,
                         flux_msg_handler_t *mh,
                         const flux_msg_t *msg,
                         void *arg)
{
    const char *rank;

    if (!(rank = flux_attr_get (h, "rank"))) {
        (void)flux_respond_error (h, msg, errno, "rank attribute is unset");
        return;
    }
    if (flux_respond_pack (h, msg, "{s:s}", "rank", rank) < 0)
        diag ("flux_respond_pack failed");
}

/* Register the handler and return; the harness runs the reactor.  The
 * handler is attached to the handle so closing it tears the handler down.
 */
static int echorank_server (flux_t *h, void *arg)
{
    flux_msg_handler_t *mh;
    struct flux_match match = FLUX_MATCH_REQUEST;

    match.topic_glob = "echorank";
    if (!(mh = flux_msg_handler_create (h, match, echorank_cb, NULL))) {
        diag ("flux_msg_handler_create failed");
        return -1;
    }
    flux_msg_handler_start (mh);
    if (flux_aux_set (h, "mh", mh, (flux_free_f)flux_msg_handler_destroy) < 0) {
        flux_msg_handler_destroy (mh);
        return -1;
    }
    return 0;
}

/* Return the rank that answered an "echorank" sent to 'nodeid', or -1 with
 * errno set from the failed RPC.
 */
static int ask_rank (flux_t *h, uint32_t nodeid)
{
    flux_future_t *f;
    const char *s;
    int rank = -1;
    int saved_errno;

    if (!(f = flux_rpc (h, "echorank", NULL, nodeid, 0)))
        return -1;
    if (flux_rpc_get_unpack (f, "{s:s}", "rank", &s) == 0)
        rank = atoi (s);
    saved_errno = errno;
    flux_future_destroy (f);
    errno = saved_errno;
    return rank;
}

int main (int argc, char *argv[])
{
    flux_t *h;
    int i;
    int errors = 0;

    plan (NO_PLAN);

    h = test_server_create_multi (SIZE, 0, echorank_server, NULL);
    ok (h != NULL,
        "test_server_create_multi size=%d works", SIZE);

    /* Each nodeid must be answered by the matching instance.  This is what
     * the single-rank server cannot do, since it ignores nodeid.
     */
    for (i = 0; i < SIZE; i++) {
        if (ask_rank (h, i) != i)
            errors++;
    }
    ok (errors == 0,
        "each nodeid is answered by its own rank (%d errors)", errors);

    ok (ask_rank (h, FLUX_NODEID_ANY) == 0,
        "FLUX_NODEID_ANY is answered by rank 0");
    /* The caller sits where a client of rank 0 would, so upstream is
     * unroutable, as it would be in a real instance.
     */
    ok (ask_rank (h, FLUX_NODEID_UPSTREAM) == -1 && errno == EHOSTUNREACH,
        "FLUX_NODEID_UPSTREAM fails with EHOSTUNREACH");

    /* An out of range rank is unroutable, as it would be in a real
     * instance.  It must fail rather than hang, since a test that addresses
     * a rank the harness does not have would otherwise never return.
     */
    ok (ask_rank (h, SIZE) == -1 && errno == EHOSTUNREACH,
        "an out of range nodeid fails with EHOSTUNREACH");

    /* Repeat to show the routing is not a one-shot.
     */
    errors = 0;
    for (i = SIZE - 1; i >= 0; i--) {
        if (ask_rank (h, i) != i)
            errors++;
    }
    ok (errors == 0,
        "ranks answer correctly when addressed in reverse (%d errors)",
        errors);

    ok (test_server_stop_multi (h) == 0,
        "test_server_stop_multi works");
    flux_close (h);

    done_testing ();
    return 0;
}

// vi:ts=4 sw=4 expandtab
