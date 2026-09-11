/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* content-gc-race MODE
 *
 * Regression test for the interaction between content-sqlite group commit
 * and the online-GC mark/sweep primitives.  Group commit holds an explicit
 * sqlite transaction open across a burst of store requests; mark and sweep
 * each open their own transaction, so they must first flush any open batch
 * (SQLite has no nested transactions, and the GC pass must see committed
 * table state).
 *
 * A shell test using the one-shot 'rpc' tool cannot exercise this: each
 * request drains the module recv queue before the next arrives, so a batch
 * is never open when mark/sweep runs.  Here we pipeline a burst of stores
 * followed immediately by a mark or sweep on a single handle -- all requests
 * are sent before any response is collected -- so the mark/sweep lands while
 * the store batch is still open.
 *
 * MODE is "mark" or "sweep".  Exit 0 if the GC RPC and every store succeed;
 * nonzero (promptly, via bounded waits) otherwise.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/log.h"
#include "src/common/libcontent/content.h"
#include "ccan/str/str.h"

/* Enough in-flight requests that the store batch is still open when the
 * trailing mark/sweep is processed, but small enough to run in milliseconds.
 */
#define BURST 200

/* Bound every wait so a reintroduced bug (which leaves the batch open and its
 * deferred store responses unsent) fails in seconds instead of hanging.
 */
#define WAIT_TIMEOUT 10.

static void get_or_die (flux_future_t *f, const char *what)
{
    if (flux_future_wait_for (f, WAIT_TIMEOUT) < 0)
        log_msg_exit ("%s: timed out (batch left uncommitted?)", what);
    if (flux_rpc_get (f, NULL) < 0)
        log_err_exit ("%s", what);
}

int main (int ac, char *av[])
{
    flux_t *h;
    const char *hashfun;
    const char *mode;
    flux_future_t *fstore[BURST];
    flux_future_t *fgc;
    char data[64];
    char blobref[BLOBREF_MAX_STRING_SIZE];
    int i;

    if (ac != 2 || (!streq (av[1], "mark") && !streq (av[1], "sweep")))
        log_msg_exit ("Usage: content-gc-race mark|sweep");
    mode = av[1];

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(hashfun = flux_attr_get (h, "content.hash")))
        log_err_exit ("getattr content.hash");

    /* Blobref of the first blob in the burst, computed locally so the mark
     * request can be built without waiting for the store response.
     */
    snprintf (data, sizeof (data), "gc-race seq=0");
    if (blobref_hash (hashfun,
                      data,
                      strlen (data),
                      blobref,
                      sizeof (blobref)) < 0)
        log_err_exit ("blobref_hash");

    /* Fire the store burst without collecting responses, so all requests
     * queue on the module and a group-commit batch stays open.
     */
    for (i = 0; i < BURST; i++) {
        snprintf (data, sizeof (data), "gc-race seq=%d", i);
        if (!(fstore[i] = content_store (h, data, strlen (data), 0)))
            log_err_exit ("content_store seq=%d", i);
    }

    /* Queue the GC RPC behind the burst, while the batch is still open. */
    if (streq (mode, "mark")) {
        if (!(fgc = flux_rpc_pack (h,
                                   "content-backing.mark",
                                   FLUX_NODEID_ANY,
                                   0,
                                   "{s:I s:[s]}",
                                   "epoch", (json_int_t)1,
                                   "hashes", blobref)))
            log_err_exit ("mark rpc");
    }
    else {
        /* Sweep everything below epoch 1 in a single generous window.  The
         * burst is stamped at epoch 0 (no checkpoint taken), so it is all
         * sweep-eligible; the point is that the sweep runs against committed
         * state after flushing the open batch, not the delete count itself.
         */
        if (!(fgc = flux_rpc_pack (h,
                                   "content-backing.sweep",
                                   FLUX_NODEID_ANY,
                                   0,
                                   "{s:I s:I s:I s:i s:i}",
                                   "epoch", (json_int_t)1,
                                   "cursor", (json_int_t)0,
                                   "high_water", (json_int_t)INT64_MAX,
                                   "delete_cap", 1000000,
                                   "window", 1000000)))
            log_err_exit ("sweep rpc");
    }

    /* Collect: the GC RPC must succeed, and so must every deferred store
     * (batch_commit within the GC handler releases their responses).
     */
    get_or_die (fgc, mode);
    flux_future_destroy (fgc);
    for (i = 0; i < BURST; i++) {
        get_or_die (fstore[i], "store");
        flux_future_destroy (fstore[i]);
    }

    flux_close (h);
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
