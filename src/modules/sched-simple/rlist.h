/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_SCHED_RLIST_H
#define HAVE_SCHED_RLIST_H 1

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <jansson.h>
#include <czmq.h>
#include <flux/idset.h>

/* A list of resource nodes */
struct rlist {
    int total;
    int avail;
    zlistx_t *nodes;
};

/*  Create an empty rlist object */
struct rlist *rlist_create (void);

/*  Create a copy of rlist rl with all cores available */
struct rlist *rlist_copy_empty (const struct rlist *rl);

/*  Create an rlist object from resource.hwloc.by_rank JSON input
 */
struct rlist *rlist_from_hwloc_by_rank (const char *by_rank);

/*  Destroy an rlist object */
void rlist_destroy (struct rlist *rl);

/*  Append a new resource node with rank==rank and idset string `ids`
 */
int rlist_append_rank (struct rlist *rl, unsigned int rank, const char *ids);

/*  Same as rlist_append_rank(), but `ids` is a struct idset
 */
int rlist_append_idset (struct rlist *rl, int rank, struct idset *ids);

/*  Return number of resource nodes in resource list `rl`
 */
size_t rlist_nnodes (struct rlist *rl);

/*
 *  Serialize a resource list into v1 "R" format. This encodes only the
 *   "available" ids in each resource node into execution.R_lite
 */
json_t *rlist_to_R (struct rlist *rl);

/*
 *  Dump short form description of rlist `rl` as a single line string.
 *    Caller must free returned string.
 */
char *rlist_dumps (struct rlist *rl);

/*
 *  De-serialize a v1 "R" format string into a new resource list object.
 *  Returns a new resource list object on success, NULL on failure.
 */
struct rlist *rlist_from_R (const char *R);

/*  Attempt to allocate nslots of slot_size across optional nnodes
 *   from the resource list `rl` using algorithm `mode`.
 *
 *  Valid modes (nnodes == 0 only):
 *   NULL or "worst-fit" - allocate from least-used nodes first
 *   "best-fit"          - allocate from most-used nodes first
 *   "first-fit"         - allocate first free slots found in rank order
 *
 *  Returns a new rlist representing the allocation on success,
 *   NULL on failure with errno set:
 *
 *   ENOSPC - unable to fulfill allocation.
 *   EINVAL - An argument was invalid.
 */
struct rlist *rlist_alloc (struct rlist *rl,
                           const char *mode,
                           int nnodes,
                           int slot_size,
                           int nslots);

/*  Remove rlist "alloc" from rlist "rl".
 */
int rlist_remove (struct rlist *rl, struct rlist *alloc);

/*  Free resource list `to_free` from resource list `rl`
 */
int rlist_free (struct rlist *rl, struct rlist *to_free);

#endif /* !HAVE_SCHED_RLIST_H */
