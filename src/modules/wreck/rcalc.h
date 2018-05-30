/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

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
