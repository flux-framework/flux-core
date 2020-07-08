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
#include <getopt.h>
#include <stdio.h>
#include <flux/core.h>

#include "src/common/libutil/read_all.h"
#include "src/common/libutil/log.h"


#define OPTIONS "r"
static const struct option longopts[] = {
    {"raw", no_argument,  0, 'r'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, "Usage: rpc [-r] topic [errnum] <payload >payload\n");
    exit (1);
}

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
    int ch;
    bool raw = false;

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'r':
                raw = true;
                break;
            default:
                usage ();
        }
    }
    if (argc - optind != 1 && argc - optind != 2)
        usage ();
    topic = argv[optind++];
    if (argc - optind > 0)
        expected_errno = strtoul (argv[optind++], NULL, 10);

    /* N.B. As a safety measure, read_all() adds a NULL char to the buffer
     * that is not accounted for in the returned length.  RFC 3 requires that
     * JSON payloads include a NULL terminator.  Assume a JSON payload
     * if size is nonzero AND the raw option was not specified, and add one
     * to the required length to satisfy RFC 3.
     */
    if ((inlen = read_all (STDIN_FILENO, &inbuf)) < 0)
        log_err_exit ("read from stdin");
    if (!raw && inlen > 0)
        inlen++;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(f = flux_rpc_raw (h, topic, inbuf, inlen, FLUX_NODEID_ANY, 0)))
        log_err_exit ("flux_rpc_raw %s", topic);
    if (flux_rpc_get_raw (f, &outbuf, &outlen) < 0) {
        if (expected_errno > 0) {
            if (errno != expected_errno)
                log_msg_exit ("%s: failed with errno=%d != expected %d",
                              topic, errno, expected_errno);
        }
        else
            log_msg_exit ("%s: %s", topic, future_strerror (f, errno));
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
