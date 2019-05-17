/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_SCHED_RNODE_H
#define HAVE_SCHED_RNODE_H 1

#if HAVE_CONFIG_H
#    include "config.h"
#endif

#include <inttypes.h>
#include <jansson.h>
#include <flux/idset.h>

/* Simple resource node object */
struct rnode {
    uint32_t rank;
    struct idset *ids;
    struct idset *avail;
};

/*  Create a resource node object from an existing idset `set`
 */
struct rnode *rnode_create_idset (uint32_t rank, struct idset *ids);

/*  Create a resource node from a string representation of an idset.
 */
struct rnode *rnode_create (uint32_t rank, const char *ids);

/*  Create a resource node with `count` ids, starting at 0, i.e.
 *   same as rnode_create (rank, "0-"..count-1).
 */
struct rnode *rnode_create_count (uint32_t rank, int count);

/*  Destroy rnode object
 */
void rnode_destroy (struct rnode *n);

/*  Allocate `count` ids from rnode object `n`.
 *   On success, return 0 and allocated ids in `setp`.
 *   On failure, return -1 with errno set:
 *   ENOSPC - there are not `count` ids available in `n`
 *   EINVAL - Invalid arguments
 */
int rnode_alloc (struct rnode *n, int count, struct idset **setp);

/*  Free the idset `ids` from resource node `n`.
 *  Returns 0 on success, -1 if one or more ids is not allocated.
 */
int rnode_free (struct rnode *n, const char *ids);

/*  As above, but free a struct idset instead of string.
 */
int rnode_free_idset (struct rnode *n, struct idset *ids);

/*  Allocate specific idset `ids` from a resource node
 */
int rnode_alloc_idset (struct rnode *n, struct idset *ids);

/*  Return the number of ids available in resource node `n`.
 */
size_t rnode_avail (const struct rnode *n);

/*  Return the total number of ids in resource node `n`.
 */
size_t rnode_count (const struct rnode *n);

#endif /* !HAVE_SCHED_RNODE_H */
