/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef SHELL_RCALC_H
#define SHELL_RCALC_H

#include <sched.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/taskmap.h>

typedef struct rcalc rcalc_t;

struct rcalc_rankinfo {
    int nodeid;               /* This rank's nodeid within the job       */
    int rank;                 /* The current broker rank                 */
    int ntasks;               /* Number of tasks assigned to this rank   */
    int ncores;               /* Number of cores allocated on this rank  */
    char cores [128];         /* String core list (directly from R_lite) */
    char gpus [128];          /* String gpu list (directly from R)       */
};

/* Create resource calc object from JSON string in "Rlite" format */
rcalc_t *rcalc_create (const char *json_in);

/* As above, but from Jansson json_t object */
rcalc_t *rcalc_create_json (json_t *R);

/* Same as above, but read JSON input from file */
rcalc_t *rcalc_createf (FILE *);

void rcalc_destroy (rcalc_t *r);

/*  Return # of total tasks contained in rcalc object */
int rcalc_total_ntasks (rcalc_t *r);
/*  Return # of total cores asssigned to rcalc object */
int rcalc_total_cores (rcalc_t *r);
/*  Return # of total gpus asssigned to rcalc object */
int rcalc_total_gpus (rcalc_t *r);
/*  Return total # of slots in rcalc object */
int rcalc_total_slots (rcalc_t *r);
/*  Return total # of nodes/ranks in rcalc object */
int rcalc_total_nodes (rcalc_t *r);
/*  Return the total # of nodes/ranks with at least 1 task assigned */
int rcalc_total_nodes_used (rcalc_t *r);
/*  Return 1 if rcalc_t contains information for rank `rank`, 0 otherwise */
int rcalc_has_rank (rcalc_t *r, int rank);

/*  Distribute ntasks across cores in r */
int rcalc_distribute (rcalc_t *r, int ntasks, int cores_per_task);
/*  Distribute ntasks *per-resource* of type `name` in `r` */
int rcalc_distribute_per_resource (rcalc_t *r, const char *name, int ntasks);

/*  Fill in rcalc_rankinfo for rank */
int rcalc_get_rankinfo (rcalc_t *r, int rank, struct rcalc_rankinfo *ri);

/*  Fill in rcalc_rankinfo for the nth rank in the rcalc_t list */
int rcalc_get_nth (rcalc_t *r, int id, struct rcalc_rankinfo *ri);

/* Update rcalc with a new taskmap
 */
int rcalc_update_map (rcalc_t *r, struct taskmap *map);

#endif /* !SHELL_RCALC_H */
