/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
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
#include <unistd.h>
#include <jansson.h>
#include <stdio.h>
#include <flux/core.h>

#include "src/common/libutil/read_all.h"
#include "src/common/libutil/log.h"

int main (int argc, char *argv[])
{
    flux_t *h;
    flux_future_t *f;
    const char *topic;
    ssize_t inlen;
    void *inbuf;
    const char *out;
    const char *end_key = NULL;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (argc != 2 && argc != 3) {
        fprintf (stderr, "Usage: rpc_stream topic [end-key] <payload\n");
        exit (1);
    }
    topic = argv[1];
    if (argc == 3)
        end_key = argv[2];

    /* N.B. As a safety measure, read_all() adds a NUL char to the buffer
     * that is not accounted for in the returned length.
     */
    if ((inlen = read_all (STDIN_FILENO, &inbuf)) < 0)
        log_err_exit ("read from stdin");
    if (!(f = flux_rpc (h,
                        topic,
                        inlen > 0 ? inbuf : NULL,
                        FLUX_NODEID_ANY,
                        FLUX_RPC_STREAMING)))
        log_err_exit ("flux_rpc %s", topic);

    bool done = false;
    do {
        if (flux_rpc_get (f, &out) < 0) {
            if (errno == ENODATA)
                break;
            log_msg_exit ("%s: %s", topic, future_strerror (f, errno));
        }
        printf ("%s\n", out);
        fflush (stdout);
        if (end_key) {
            json_t *o = json_loads (out, 0, NULL);
            if (!o)
                log_msg_exit ("failed to parse response payload");
            if (json_object_get (o, end_key))
                done = true;
            json_decref (o);
        }
        if (!done)
            flux_future_reset (f);
    } while (!done);
    flux_future_destroy (f);
    free (inbuf);
    flux_close (h);
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
