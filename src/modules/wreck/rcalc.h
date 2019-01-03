/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_RCALC_H
#define HAVE_RCALC_H

#include <sched.h>
#include <flux/core.h>

typedef struct rcalc rcalc_t;

struct rcalc_rankinfo {
    int nodeid;               /* This rank's nodeid within the job       */
    int rank;                 /* The current broker rank                 */
    int ntasks;               /* Number of tasks assigned to this rank   */
    int global_basis;         /* Task id of the first task on this rank  */
    int ncores;               /* Number of cores allocated on this rank  */
    cpu_set_t cpuset;         /* cpu_set_t representation of cores list  */
    char cores [128];         /* String core list (directly from R_lite) */
};

/* Create resource calc object from JSON string in "Rlite" format */
rcalc_t *rcalc_create (const char *json_in);
/* Same as above, but read JSON input from file */
rcalc_t *rcalc_createf (FILE *);

void rcalc_destroy (rcalc_t *r);

/*  Return # of total cores asssigned to rcalc object */
int rcalc_total_cores (rcalc_t *r);
/*  Return total # of nodes/ranks in rcalc object */
int rcalc_total_nodes (rcalc_t *r);
/*  Return the total # of nodes/ranks with at least 1 task assigned */
int rcalc_total_nodes_used (rcalc_t *r);
/*  Return 1 if rcalc_t contains information for rank `rank`, 0 otherwise */
int rcalc_has_rank (rcalc_t *r, int rank);

/*  Distribute ntasks across cores in r */
int rcalc_distribute (rcalc_t *r, int ntasks);

/*  Fill in rcalc_rankinfo for rank */
int rcalc_get_rankinfo (rcalc_t *r, int rank, struct rcalc_rankinfo *ri);

/*  Fill in rcalc_rankinfo for the nth rank in the rcalc_t list */
int rcalc_get_nth (rcalc_t *r, int id, struct rcalc_rankinfo *ri);

#endif /* !HAVE_RCALC_H */
