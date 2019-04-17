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
    const void *outbuf;
    int outlen;
    int expected_errno = -1;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (argc != 2 && argc != 3) {
        fprintf (stderr, "Usage: rpc topic [errnum] <payload >payload\n");
        exit (1);
    }
    topic = argv[1];
    if (argc == 3)
        expected_errno = strtoul (argv[2], NULL, 10);

    if ((inlen = read_all (STDIN_FILENO, &inbuf)) < 0)
        log_err_exit ("read from stdin");

    if (!(f = flux_rpc_raw (h, topic, inbuf, inlen, FLUX_NODEID_ANY, 0)))
        log_err_exit ("flux_rpc_raw %s", topic);
    if (flux_rpc_get_raw (f, &outbuf, &outlen) < 0) {
        if (expected_errno > 0) {
            if (errno != expected_errno)
                log_msg_exit ("%s: failed with errno=%d != expected %d",
                              topic, errno, expected_errno);
        }
        else
            log_err_exit ("%s", topic);
    }
    else {
        if (expected_errno > 0)
            log_msg_exit ("%s: succeeded but expected failure errno=%d",
                          topic, expected_errno);
        if (write_all (STDOUT_FILENO, outbuf, outlen) < 0)
            log_err_exit ("write to stdout");
    }
    flux_future_destroy (f);
    free (inbuf);
    flux_close (h);
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
