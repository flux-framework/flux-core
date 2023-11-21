/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* idsetutil.c - command line idset calculator for testing */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "src/common/libidset/idset.h"
#include "src/common/libutil/read_all.h"
#include "ccan/str/str.h"


int expand (int argc, char **argv)
{
    struct idset *ids = NULL;
    char *input;
    char *buf = NULL;
    unsigned int id;

    if (argc != 0 && argc != 1) {
        fprintf (stderr, "Usage: idsetutil expand [IDSET]\n");
        return -1;
    }
    if (argc == 0) {
        if (read_all (STDIN_FILENO, (void **)&buf) < 0) {
            fprintf (stderr, "error reading stdin: %s\n", strerror (errno));
            goto error;
        }
        if (buf[strlen (buf) - 1] == '\n') // drop trailing newline, if any
            buf[strlen (buf) - 1] = '\0';
        input = buf;
    }
    else
        input = argv[0];
    if (!(ids = idset_decode (input))) {
        fprintf (stderr, "error decoding idset: %s\n", strerror (errno));
        goto error;
    }
    id = idset_first (ids);
    while (id != IDSET_INVALID_ID) {
        printf ("%u\n", id);
        id = idset_next (ids, id);
    }
    idset_destroy (ids);
    free (buf);
    return 0;
error:
    idset_destroy (ids);
    free (buf);
    return -1;
}

void usage (void)
{
    fprintf (stderr,
             "Usage: idsetutil CMD ARGS\n"
             "where CMD is one of:\n"
             "expand [IDSET]\n"
             );
}

int main (int argc, char *argv[])
{
    if (argc < 2 || !streq (argv[1], "expand")) {
        fprintf (stderr, "Usage: isdetutil expand IDSET\n");
        return 1;
    }
    if (expand (argc - 2, argv + 2) < 0)
        return 1;

    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
