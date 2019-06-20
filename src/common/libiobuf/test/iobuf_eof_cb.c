/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* iobuf-eof-cb.c - iobuf test eof callback */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libiobuf/iobuf.h"
#include "src/common/libutil/log.h"

int eof_cb_called = 0;

void eof_count_cb (iobuf_t *iob, void *arg)
{
    flux_reactor_t *r = arg;
    flux_reactor_stop (r);
    eof_cb_called++;
}

int main (int argc, char *argv[])
{
    flux_t *h;
    iobuf_t *iob = NULL;
    int maxbuffers = 4;
    int eofcount = 4;
    int i;

    plan (NO_PLAN);

    (void)setenv ("FLUX_CONNECTOR_PATH",
                  flux_conf_get ("connector_path", CONF_FLAG_INTREE), 0);

    ok ((h = flux_open ("loop://", 0)) != NULL,
        "flux_open success");

    ok ((iob = iobuf_server_create (h,
                                    "eof-cb-tests",
                                    maxbuffers,
                                    IOBUF_FLAG_LOG_ERRORS)) != NULL,
        "iobuf_server_create success");

    ok (!iobuf_set_eof_count_cb (iob,
                                 eofcount,
                                 eof_count_cb,
                                 flux_get_reactor (h)),
        "iobuf_set_eof_count_cb success");

    for (i = 0; i < maxbuffers; i++) {
        ok (!iobuf_eof (iob, "test-eof-cb", i + 1),
            "iobuf_eof success");
    }

    ok (!(flux_reactor_run (flux_get_reactor (h), 0) < 0),
        "flux_reactor_run exited");

    ok (eof_cb_called == 1,
        "eof count callback called correctly");

    done_testing ();

    iobuf_server_destroy (iob);
    flux_close (h);
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
