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
#include <stdint.h>
#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#include "src/common/libutil/monotime.h"

static inline void monotime_diff (struct timespec *t0,
                                  struct timespec *result)
{
    struct timespec now;
    monotime (&now);
    result->tv_sec  = now.tv_sec - t0->tv_sec;
    result->tv_nsec = now.tv_nsec - t0->tv_nsec;
    if (result->tv_nsec < 0) {
        --result->tv_sec;
        result->tv_nsec += 1000000000L;
    }
}

static void die (const char *msg)
{
    fprintf (stderr, "%s\n", msg);
    exit (1);
}

int main (int argc, char *argv[])
{
    int id, ntasks;
    struct timespec t0, t, times[4];
    const char *label;
    bool timing = getenv ("FLUX_MPI_TEST_TIMING");

    if (!(label = getenv ("FLUX_JOB_CC")))
        if (!(label = getenv ("FLUX_JOB_ID")))
            label = "0";

    monotime (&t0);
    if (MPI_Init (&argc, &argv) != MPI_SUCCESS)
        die ("MPI_Init failed\n");
    if (MPI_Comm_rank (MPI_COMM_WORLD, &id) != MPI_SUCCESS
        || MPI_Comm_size (MPI_COMM_WORLD, &ntasks) != MPI_SUCCESS)
        die ("MPI_Comm_rank/size failed");
    monotime_diff (&t0, &times[0]);
    if (!timing && id == 0) {
        printf ("%s: completed MPI_Init in %0.3fs. There are %d tasks\n",
                label,
                monotime_since (t0) / 1000,
                ntasks);
        fflush (stdout);
    }

    monotime (&t);
    if (MPI_Barrier (MPI_COMM_WORLD) != MPI_SUCCESS)
        die ("MPI_Barrier failed");
    monotime_diff (&t, &times[1]);
    if (!timing && id == 0) {
        printf ("%s: completed first barrier in %0.3fs\n",
                label,
                monotime_since (t) / 1000);
        fflush (stdout);
    }

    monotime (&t);
    MPI_Finalize ();
    monotime_diff (&t, &times[2]);
    monotime_diff (&t0, &times[3]);

    if (id == 0) {
        if (timing) {
            printf ("%6s %8d", getenv ("FLUX_JOB_NNODES"), ntasks);
            for (int i = 0; i < 4; i++)
                printf (" %4ju.%.9ld",
                        (uintmax_t) times[i].tv_sec,
                        times[i].tv_nsec);
            printf ("\n");
        }
        else
            printf ("%s: completed MPI_Finalize in %0.3fs\n",
                    label,
                    monotime_since (t) / 1000);
        fflush (stdout);
    }
    return 0;
}

// vi: ts=4 sw=4 expandtab

