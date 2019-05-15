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
#include <getopt.h>
#include <flux/core.h>
#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>
#include <jansson.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libutil/read_all.h"

int main (int argc, char *argv[])
{
    uint8_t *data;
    int size;
    char *hashtype;
    char blobref[BLOBREF_MAX_STRING_SIZE];

    if (argc != 2) {
        fprintf (stderr, "Usage: cat file | blobref hashtype\n");
        exit (1);
    }
    hashtype = argv[1];

    if ((size = read_all (STDIN_FILENO, (void **)&data)) < 0)
        log_err_exit ("read");

    if (blobref_hash (hashtype, data, size, blobref, sizeof (blobref)) < 0)
        log_err_exit ("blobref_hash");
    printf ("%s\n", blobref);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
