/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* simple tool that outputs args to stdout/stderr or both depending on
 * options
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

int out = 0;
int err = 0;
int channel_out = 0;
int no_newline = 0;
char *channel_name;
int channel = 0;
int fd = STDIN_FILENO;
int prefix = 0;

void output (const char *str)
{
    if (channel && channel_out) {
        char prefixbuf[1024];
        int plen;

        plen = sprintf (prefixbuf,
                        "%s%s%s",
                        prefix ? channel_name : "",
                        prefix ? ":" : "",
                        str);
        if (write (fd, prefixbuf, plen) < 0) {
            perror ("write");
            exit (1);
        }
    }
    if (out) {
        fprintf (stdout,
                 "%s%s",
                 prefix ? "STDOUT:" : "",
                 str);
        fflush (stdout);
    }
    if (err) {
        fprintf (stderr,
                 "%s%s",
                 prefix ? "STDERR:" : "",
                 str);
        fflush (stderr);
    }
}

int
main (int argc, char *argv[])
{
    int bytes = 0;

    while (1) {
        int c = getopt (argc, argv, "c:COEnPb:");
        if (c < 0)
            break;

        switch (c) {
        case 'O':
            out++;
            break;
        case 'E':
            err++;
            break;
        case 'n':
            no_newline++;
            break;
        case 'c':
            channel++;
            channel_name = optarg;
            break;
        case 'C':
            channel_out++;
            break;
        case 'P':
            prefix++;
            break;
        case 'b':
            bytes = atoi (optarg);
            break;
        }
    }

    if ((out + err) == 0
        && (!channel && !channel_out)) {
        fprintf (stderr, "must specify a way to output");
        exit (1);
    }

    if (channel) {
        const char *fdstr;
        char channelstr[1024];

        sprintf (channelstr, "%s", channel_name);

        if (!(fdstr = getenv (channelstr))) {
            perror ("getenv");
            exit (1);
        }

        fd = atoi (fdstr);
    }

    if (optind != argc) {
        while (optind < argc) {
            char outbuf[1024];

            sprintf (outbuf,
                     "%s%s",
                     argv[optind],
                     no_newline ? "" : "\n");

            output (outbuf);
            optind++;
        }
    }
    else {
        char buf[1024];
        int total = 0;
        int len;

        memset (buf, '\0', 1024);
        while ((len = read (fd, buf, 1024)) > 0) {
            char outbuf[1025]; /* add extra char for -Werror=format-overflow */

            sprintf (outbuf,
                     "%s%s",
                     buf,
                     no_newline ? "" : "\n");

            output (outbuf);

            total += len;

            if (bytes && total >= bytes)
                break;

            memset (buf, '\0', 1024);
        }
    }

    close (STDOUT_FILENO);
    close (STDERR_FILENO);
    exit (0);
}
