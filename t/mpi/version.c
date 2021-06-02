/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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
#include <mpi.h>
#include <stdio.h>

int main (int argc, char *argv[])
{
    char version[MPI_MAX_LIBRARY_VERSION_STRING];
    int len;
    int exit_rc = -1;

    MPI_Get_library_version (version, &len);
    if (len < 0) {
        fprintf (stderr, "MPI_Get_library_version failed\n");
        goto done;
    }
    printf ("%s\n", version);
    exit_rc = 0;
done:
    return exit_rc;
}

// vi: ts=4 sw=4 expandtab

