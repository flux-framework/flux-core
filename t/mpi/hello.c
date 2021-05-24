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
#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#include "src/common/libutil/monotime.h"

int main (int argc, char *argv[])
{
    int id, ntasks;
    struct timespec t;

    monotime (&t);
    MPI_Init (&argc, &argv);
    MPI_Comm_rank (MPI_COMM_WORLD, &id);
    MPI_Comm_size (MPI_COMM_WORLD, &ntasks);
    if (id == 0) {
        printf ("0: completed MPI_Init in %0.3fs.  There are %d tasks\n",
                monotime_since (t) / 1000, ntasks);
        fflush (stdout);
    }

    monotime (&t);
    MPI_Barrier (MPI_COMM_WORLD);
    if (id == 0) {
        printf ("0: completed first barrier in %0.3fs\n",
                monotime_since (t) / 1000);
        fflush (stdout);
    }

    monotime (&t);
    MPI_Finalize ();
    if (id == 0) {
        printf ("0: completed MPI_Finalize in %0.3fs\n",
                monotime_since (t) / 1000);
        fflush (stdout);
    }
    return 0;
}

// vi: ts=4 sw=4 expandtab

