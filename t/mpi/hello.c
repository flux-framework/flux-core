/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

static struct timespec diff (struct timespec start, struct timespec end)
{
    struct timespec temp;
    if ((end.tv_nsec-start.tv_nsec)<0) {
        temp.tv_sec = end.tv_sec-start.tv_sec-1;
        temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
    } else {
        temp.tv_sec = end.tv_sec-start.tv_sec;
        temp.tv_nsec = end.tv_nsec-start.tv_nsec;
    }
    return temp;
}

static double time_since (struct timespec t0)
{
    struct timespec ts, d;
    clock_gettime (CLOCK_MONOTONIC, &ts);

    d = diff (t0, ts);

    return ((double) d.tv_sec * 1000 + (double) d.tv_nsec / 1000000);
}

int
main(int argc, char *argv[])
{
        int id, ntasks;
	struct timespec t;

	clock_gettime (CLOCK_MONOTONIC, &t);
        MPI_Init(&argc, &argv);
        MPI_Comm_rank(MPI_COMM_WORLD, &id);
        MPI_Comm_size(MPI_COMM_WORLD, &ntasks);
        if (id == 0) {
                printf("0: completed MPI_Init in %0.3fs.  There are %d tasks\n",
		   time_since (t)/1000, ntasks);
                fflush(stdout);
        }

	if (id == 0)
		clock_gettime (CLOCK_MONOTONIC, &t);
        MPI_Barrier(MPI_COMM_WORLD);
        if (id == 0) {
                printf("0: completed first barrier in %0.3fs\n",
				time_since (t) / 1000);
                fflush(stdout);
        }

	if (id == 0)
		clock_gettime (CLOCK_MONOTONIC, &t);
        MPI_Finalize();
        if (id == 0) {
                printf("0: completed MPI_Finalize in %0.3fs\n",
				time_since (t) / 1000);
                fflush(stdout);
        }
        return 0;
}
