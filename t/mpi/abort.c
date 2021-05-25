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
#include <stdlib.h>
#include <mpi.h>

int main (int argc, char *argv[])
{
    int id, ntasks;
    int abort_rank = -1;

    if (argc == 2)
        abort_rank = strtol (argv[1], NULL, 10);

    MPI_Init (&argc, &argv);
    MPI_Comm_rank (MPI_COMM_WORLD, &id);
    MPI_Comm_size (MPI_COMM_WORLD, &ntasks);

    if (id == abort_rank)
        MPI_Abort (MPI_COMM_WORLD, 42);
    MPI_Barrier (MPI_COMM_WORLD);

    MPI_Finalize ();

    return 0;
}

// vi: ts=4 sw=4 expandtab

