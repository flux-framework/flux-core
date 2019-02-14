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
#include <jansson.h>
#include <unistd.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"

#define MAX_ITERS 50

/* A kvs lookup API doesn't accept a rank, so we cheat and build the
 * kvs-watch.lookup request message from scratch here.  We have to set
 * FLUX_KVS_WAITCREATE, to ensure the lookup "hangs" for this test.
 */
void send_watch_requests (flux_t *h, const char *key)
{
    flux_mrpc_t *r;

    if (!(r = flux_mrpc_pack (h, "kvs-watch.lookup", "all",
                              FLUX_RPC_STREAMING,
                              "{s:s s:s s:i}",
                              "key", key,
                              "namespace", KVS_PRIMARY_NAMESPACE,
                              "flags", FLUX_KVS_WATCH | FLUX_KVS_WAITCREATE)))
        log_err_exit ("flux_mrpc kvs-watch.lookup");

    flux_mrpc_destroy (r);
}

/* Sum #watchers over all ranks.
 */
int count_watchers (flux_t *h)
{
    int n, count = 0;
    flux_mrpc_t *r;

    if (!(r = flux_mrpc (h, "kvs-watch.stats.get", NULL, "all", 0)))
        log_err_exit ("flux_mrpc kvs-watch.stats.get");
    do {
        if (flux_mrpc_get_unpack (r, "{ s:i }", "watchers", &n) < 0)
            log_err_exit ("kvs-watch.stats.get");
        count += n;
    } while (flux_mrpc_next (r) == 0);
    flux_mrpc_destroy (r);
    return count;
}

static void usage (void)
{
    fprintf (stderr, "Usage: watch_disconnect <rankcount>\n");
    exit (1);
}

int main (int argc, char **argv)
{
    flux_t *h;
    int i, rankcount, w0, w1, w2;

    if (argc != 2)
        usage();

    rankcount = strtoul (argv[1], NULL, 10);
    if (!rankcount)
        log_msg_exit ("rankcount must be > 0");

    /* Install watchers on every rank, then disconnect.
     * The number of watchers should return to the original count.
     */
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    w0 = count_watchers (h);
    send_watch_requests (h, "nonexist");

    /* must spin / wait for watchers to be registered */
    for (i = 0; i < MAX_ITERS; i++) {
        w1 = count_watchers (h) - w0;
        if (w1 == rankcount)
            break;
        usleep (100000);
    }

    log_msg ("test watchers: %d", w1);
    flux_close (h);
    log_msg ("disconnected");


    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    /* must spin / wait for disconnects to be processed */
    for (i = 0; i < MAX_ITERS; i++) {
        w2 = count_watchers (h) - w0;
        if (w2 == 0)
            break;
        usleep (100000);
    }

    log_msg ("test watchers: %d", w2);
    if (w2 != 0)
        log_err_exit ("Test failure, watchers were not removed on disconnect");

    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
