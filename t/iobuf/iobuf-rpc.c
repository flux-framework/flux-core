/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* iobuf.c - iobuf ops */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <flux/core.h>

#include "src/common/libiobuf/iobuf.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"

flux_t *h;
char *name = NULL;
char *stream = NULL;
int rank = -1;

void usage (void)
{
    fprintf (stderr,
"Usage: iobuf create <name> <stream> <rank>\n"
"Usage: iobuf write  <name> <stream> <rank> <stringinput>\n"
"Usage: iobuf read   <name> <stream> <rank>\n"
"Usage: iobuf eof    <name> <stream> <rank>\n"
);
    exit (1);
}

void parse_stream_rank (int argc, char *argv[])
{
    if (argc < 5)
        usage ();

    stream = argv[3];
    rank = atoi (argv[4]);
    if (rank < 0)
        log_err_exit ("invalid rank");
}

void createcmd (int argc, char *argv[])
{
    flux_future_t *f;

    parse_stream_rank (argc, argv);

    if (!(f = iobuf_rpc_create (h, name, 0,
                                stream, rank)))
        log_err_exit ("iobuf_rpc_create");

    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_future_get");

    flux_future_destroy (f);
}

void writecmd (int argc, char *argv[])
{
    flux_future_t *f;

    if (argc != 6)
        usage ();

    parse_stream_rank (argc, argv);

    if (!(f = iobuf_rpc_write (h, name, 0,
                               stream, rank,
                               argv[5], strlen (argv[5]))))
        log_err_exit ("iobuf_rpc_write");
    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("iobuf_rpc_write: flux_future_get");
}

void readcmd (int argc, char *argv[])
{
    flux_future_t *f;
    char *data = NULL;
    int data_len = -1;

    parse_stream_rank (argc, argv);

    if (!(f = iobuf_rpc_read (h, name, 0,
                              stream, rank)))
        log_err_exit ("iobuf_rpc_read");

    /* we call two times for coverage */

    if (iobuf_rpc_read_get (f, &data, NULL) < 0)
        log_err_exit ("iobuf_rpc_read_get: data");

    if (iobuf_rpc_read_get (f, NULL, &data_len) < 0)
        log_err_exit ("iobuf_rpc_read_get: data");

    if (data)
        printf ("data: %s\n", data);
    printf ("data_len: %d\n", data_len);

    free (data);
    flux_future_destroy (f);
}

void eofcmd (int argc, char *argv[])
{
    flux_future_t *f;

    parse_stream_rank (argc, argv);

    if (!(f = iobuf_rpc_eof (h, name, 0, stream, rank)))
        log_err_exit ("iobuf_rpc_eof");

    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_future_get");

    flux_future_destroy (f);
}

int main (int argc, char *argv[])
{
    char *cmd = NULL;

    log_init ("iobuf");

    if (argc < 3)
        usage ();

    cmd = argv[1];
    name = argv[2];

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!strcmp (cmd, "create"))
        createcmd (argc, argv);
    else if (!strcmp (cmd, "write"))
        writecmd (argc, argv);
    else if (!strcmp (cmd, "read"))
        readcmd (argc, argv);
    else if (!strcmp (cmd, "eof"))
        eofcmd (argc, argv);
    else
        log_err_exit ("invalid cmd: %s\n", cmd);

    flux_close (h);
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
