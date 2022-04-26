/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Usage: content-spam N [M]
 * Store N random entries, keeping M requests in flight (default 1)
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <flux/core.h>

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/log.h"
#include "src/common/libcontent/content.h"

static int spam_max_inflight;
static int spam_cur_inflight;

static void store_completion (flux_future_t *f, void *arg)
{
    flux_t *h = arg;
    const char *blobref;

    if (content_store_get_blobref (f, &blobref) < 0)
        log_err_exit ("store");
    printf ("%s\n", blobref);
    flux_future_destroy (f);
    if (--spam_cur_inflight < spam_max_inflight/2)
        flux_reactor_stop (flux_get_reactor (h));
}

int main (int ac, char *av[])
{
    int i, count;
    flux_future_t *f;
    flux_t *h;
    char data[256];
    int size = 256;

    if (ac != 2 && ac != 3) {
        fprintf (stderr, "Usage: content-spam N [M]\n");
        exit (1);
    }
    count = strtoul (av[1], NULL, 10);
    if (ac == 3)
        spam_max_inflight = strtoul (av[2], NULL, 10);
    else
        spam_max_inflight = 1;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    spam_cur_inflight = 0;
    i = 0;
    while (i < count || spam_cur_inflight > 0) {
        while (i < count && spam_cur_inflight < spam_max_inflight) {
            snprintf (data, size, "spam-o-matic pid=%d seq=%d", getpid(), i);
            if (!(f = content_store (h, data, size, 0)))
                log_err_exit ("content_store(%d)", i);
            if (flux_future_then (f, -1., store_completion, h) < 0)
                log_err_exit ("flux_future_then(%d)", i);
            spam_cur_inflight++;
            i++;
        }
        if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
            log_err ("flux_reactor_run");
    }
    flux_close (h);
    exit (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
