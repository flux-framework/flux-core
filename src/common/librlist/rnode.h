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
#include "config.h"
#endif

#include <inttypes.h>
#include <jansson.h>
#include <flux/idset.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

struct rnode_child {
    char *name;
    struct idset *ids;
    struct idset *avail;
};

/* Simple resource node object */
struct rnode {
    bool up;
    char *hostname;
    uint32_t rank;

    struct rnode_child *cores;

    /* non-core children */
    zhashx_t *children;

    zhashx_t *properties;
};

struct rnode *rnode_new (const char *name, uint32_t rank);

struct rnode_child * rnode_add_child_idset (struct rnode *n,
                                            const char *name,
                                            const struct idset *ids,
                                            const struct idset *avail);

/*  Create a resource node object from an existing idset `set`
 */
struct rnode *rnode_create_idset (const char *name,
                                  uint32_t rank,
                                  struct idset *ids);

/*  Create a resource node from a string representation of an idset.
 */
struct rnode *rnode_create (const char *name,
                            uint32_t rank,
                            const char *ids);

/*  Create a resource node with `count` ids, starting at 0, i.e.
 *   same as rnode_create (rank, "0-"..count-1).
 */
struct rnode *rnode_create_count (const char *name,
                                  uint32_t rank,
                                  int count);

struct rnode *rnode_create_children (const char *name,
                                     uint32_t rank,
                                     json_t *children);

struct rnode_child *rnode_add_child (struct rnode *n,
                                     const char *name,
                                     const char *ids);

/*  Copy rnode 'n' */
struct rnode *rnode_copy (const struct rnode *n);

/*  Copy rnode 'n' with all allocated resources cleared */
struct rnode *rnode_copy_empty (const struct rnode *n);

/*  Copy only available resources from rnode 'n' */
struct rnode *rnode_copy_avail (const struct rnode *n);

/*  Copy only allocated resources from rnode 'n' */
struct rnode *rnode_copy_alloc (const struct rnode *n);

/*  Copy only all cores in rnode 'n' */
struct rnode *rnode_copy_cores (const struct rnode *n);

int rnode_add (struct rnode *orig, struct rnode *n);

/*  Return an rnode object that is the set difference of 'b' from 'a'.
 */
struct rnode *rnode_diff (const struct rnode *a, const struct rnode *b);

/*  Like rnode_diff() above, but ignore resource types in ignore_mask
 */
enum {
    RNODE_IGNORE_CORE = 1,
    RNODE_IGNORE_GPU = 2,
};
struct rnode *rnode_diff_ex (const struct rnode *a,
                             const struct rnode *b,
                             int ignore_mask);

/*  Return an rnode object that is the set intersection of 'a' and 'b'.
 */
struct rnode *rnode_intersect (const struct rnode *a, const struct rnode *b);

/*  Return true if rnode is empty -- i.e. it has no resources
 */
bool rnode_empty (const struct rnode *n);

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

/*  Return the number of core ids available in resource node `n`.
 */
size_t rnode_avail (const struct rnode *n);

/*  Return total of all available resources in 'n'. Returns 0 if node is
 *    not in the up state.
 */
int rnode_avail_total (const struct rnode *n);

/*  Return the total number of ids in resource node `n`.
 */
size_t rnode_count (const struct rnode *n);

/*  Return the total number of ids for resource type in node 'n'
 */
size_t rnode_count_type (const struct rnode *n, const char *type);

int rnode_cmp (const struct rnode *a, const struct rnode *b);

/*  Return 0 if hostnames of rnode 'a' and 'b' match.
 */
int rnode_hostname_cmp (const struct rnode *a, const struct rnode *b);

/*  Remap all resource IDs in rnode 'n' to zero origin
 */
int rnode_remap (struct rnode *n, zhashx_t *noremap);

json_t *rnode_encode (const struct rnode *n, const struct idset *ids);

/*  Set/remove/check for rnode properties
 */
int rnode_set_property (struct rnode *n, const char *name);

void rnode_remove_property (struct rnode *n, const char *name);

bool rnode_has_property (struct rnode *n, const char *name);

#endif /* !HAVE_SCHED_RNODE_H */
