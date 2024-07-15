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
#include <flux/idset.h>
#include <flux/hostlist.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libflux/types.h" /* flux_error_t */

/* A list of resource nodes */
struct rlist {
    int total;
    int avail;
    zlistx_t *nodes;

    zhashx_t *rank_index;

    /*  hash of resources to ignore on remap */
    zhashx_t *noremap;

    /*  Hash of property->idset mapping */
    zhashx_t *properties;

    /*  Rv1 optional starttime, expiration:
     */
    double starttime;
    double expiration;

    /*  Opaque Rv1.scheduling key */
    json_t *scheduling;
};

struct rlist_alloc_info {
    int nnodes;
    int slot_size;
    int nslots;
    const char *mode;
    bool exclusive;
    json_t *constraints;
};

/*  Create an empty rlist object */
struct rlist *rlist_create (void);

/*  Mark ranks down
 */
int rlist_mark_down (struct rlist *rl, const char *ids);

/*  Mark ranks up
 */
int rlist_mark_up (struct rlist *rl, const char *ids);

/*  Create a copy of rlist rl with all cores available */
struct rlist *rlist_copy_empty (const struct rlist *rl);

/*  Create a copy of rl including only down resources */
struct rlist *rlist_copy_down (const struct rlist *orig);

/*  Create a copy of rl including only allocated resources */
struct rlist *rlist_copy_allocated (const struct rlist *orig);

/*  Create a copy of rl including only the ranks in 'ranks' idset */
struct rlist *rlist_copy_ranks (const struct rlist *rl, struct idset *ranks);

struct rlist *rlist_copy_cores (const struct rlist *rl);

/*  Create a copy of rl constrained by an RFC 31 constraint object
 *
 *  Returns a copy of rl with only those resource nodes that match
 *   the provided constraint. The result 'struct rlist' may be empty
 *   if no resources satisfy the constraint.
 *
 *  Returns NULL with `errp` set if the constraint object was invalid.
 *
 */
struct rlist *rlist_copy_constraint (const struct rlist *rl,
                                     json_t *constraint,
                                     flux_error_t *errp);

/*  Same as above, but takes a JSON string instead of json_t object.
 */
struct rlist *rlist_copy_constraint_string (const struct rlist *orig,
                                            const char *constraint,
                                            flux_error_t *errp);

/*  Delete ranks in idset 'ranks' from rlist 'rl'
 */
int rlist_remove_ranks (struct rlist *rl, struct idset *ranks);

/*  Re-map ranks and all resources (except those named in rl->noremap hash)
 *   such that their IDs will be mapped 0 - count-1.
 */
int rlist_remap (struct rlist *rl);

/*  Re-assign hostnames to rlist 'rl'. The number of hosts in the "hosts"
 *   hostlist expression must match the size of rlist 'rl'.
 */
int rlist_assign_hosts (struct rlist *rl, const char *hosts);

/*  Re-assign ranks based on the RFC29 hostlist 'hosts'. Ranks in 'rl'
 *   will be remapped based on host order in 'hosts', i.e. the first
 *   host will be rank 0, the next rank 1, and so on.
 *
 *  Returns 0 on success, and -1 with errno set for the following cases:
 *   EOVERFLOW: the number of hostnames in 'hosts' is > nranks in 'rl'
 *   ENOSPC:    the number of hostnames in 'hosts' is < nranks in 'rl'
 *   ENOENT:    a hostname in 'hosts' was not found in 'rl'
 *   ENOMEM:    out of memory
 */
int rlist_rerank (struct rlist *rl, const char *hosts, flux_error_t *error);

/*  Destroy an rlist object */
void rlist_destroy (struct rlist *rl);

/*  Append a new resource node with hostname, rank, and core idset string
 */
int rlist_append_rank_cores (struct rlist *rl,
                             const char *hostname,
                             unsigned int rank,
                             const char *core_ids);

/*  Add child resource 'ids' with name 'name' to rank 'rank' in resource
 *   list 'rl'.
 */
int rlist_rank_add_child (struct rlist *rl,
                          unsigned int rank,
                          const char *name,
                          const char *ids);

/*  Append rlist 'rl2' to 'rl'
 */
int rlist_append (struct rlist *rl, const struct rlist *rl2);

/*  Like append, but it is not an error if resources in `rl` also
 *   exist in `rl2`.
 */
int rlist_add (struct rlist *rl, const struct rlist *rl2);

/*  Return the set difference of 'rlb' from 'rla'.
 */
struct rlist *rlist_diff (const struct rlist *rla, const struct rlist *rlb);

/*  Return the union of 'rla' and 'rlb'
 */
struct rlist *rlist_union (const struct rlist *rla, const struct rlist *rlb);

/*  Return the intersection of 'rla' and 'rlb'
 */
struct rlist *rlist_intersect (const struct rlist *rla,
                              const struct rlist *rlb);

/*  Return number of resource nodes in resource list `rl`
 */
size_t rlist_nnodes (const struct rlist *rl);

size_t rlist_count (const struct rlist *rl, const char *type);


/*  Return a hostlist of rlist hostnames
 */
struct hostlist * rlist_nodelist (const struct rlist *rl);

/*  Return an idset of rlist ranks
 */
struct idset * rlist_ranks (const struct rlist *rl);


/*  Return an idset of ranks corresponding to 'hosts' (a string encoded
 *   in RFC29 hostlist format)
 *
 *  Multiple ranks may be returned per host in 'hosts' if ranks
 *   share hostnames (e.g. multiple broker ranks per node)
 *
 *  Order of 'hosts' is ignored since the return type is an idset.
 *
 *  Returns success only if all hosts have one or more ranks in rlist.
 *
 *  Returns NULL on failure with error text in err if err is non-NULL.
 */
struct idset * rlist_hosts_to_ranks (const struct rlist *rl,
                                     const char *hosts,
                                     flux_error_t *err);

/*
 *  Serialize a resource list into v1 "R" format. This encodes only the
 *   "available" ids in each resource node into execution.R_lite
 */
json_t * rlist_to_R (struct rlist *rl);


/*
 *  Encode resource list into v1 "R" string format.
 *  Identical to `R = rlist_to_R (rl); return json_dumps (R, 0);`.
 */
char *rlist_encode (struct rlist *rl);

/*
 *  Dump short form description of rlist `rl` as a single line string.
 *    Caller must free returned string.
 */
char *rlist_dumps (const struct rlist *rl);

/*
 *  De-serialize a v1 "R" format string into a new resource list object.
 *  Returns a new resource list object on success, NULL on failure.
 */
struct rlist *rlist_from_R (const char *R);

/*  Like rlist_from_R(), but takes a json_t * argument.
 */
struct rlist *rlist_from_json (json_t *o, json_error_t *err);

struct rlist *rlist_from_hwloc (int my_rank, const char *xml);

/*  Verify resources in rlist 'actual' meet or exceed resources in
 *   matching ranks of rlist 'expected'
 *  Returns:
 *
 *    0: all resources in matching ranks of 'expected' are in 'actual'
 *
 *   -1: one or more resources in 'expected' do not appear in 'actual'
 *        a human readable summary will be available in error.text if
 *        error is non-NULL.
 *
 *    1: resources in 'actual' exceed those in 'expected'.
 */
int rlist_verify (flux_error_t *error,
                  const struct rlist *expected,
                  const struct rlist *actual);

/*  Attempt to allocate nslots of slot_size across optional nnodes
 *   from the resource list `rl` using algorithm `mode`.
 *
 *  Valid modes (nnodes == 0 only):
 *   NULL or "worst-fit" - allocate from least-used nodes first
 *   "best-fit"          - allocate from most-used nodes first
 *   "first-fit"         - allocate first free slots found in rank order
 *
 *  Returns a new rlist representing the allocation on success,
 *   NULL on failure with errno set.
 *
 *   ENOSPC - unable to fulfill allocation.
 *   EINVAL - An argument was invalid.
 */
struct rlist * rlist_alloc (struct rlist *rl,
                            const struct rlist_alloc_info *ai,
                            flux_error_t *errp);

/*  Mark rlist "alloc" as allocated in rlist "rl".
 */
int rlist_set_allocated (struct rlist *rl, struct rlist *alloc);

/*  Free resource list `to_free` from resource list `rl`
 */
int rlist_free (struct rlist *rl, struct rlist *to_free);

/*  Assign a single property 'name' to ranks in 'targets'
 */
int rlist_add_property (struct rlist *rl,
                        flux_error_t *errp,
                        const char *name,
                        const char *targets);

/*  Assign properties to targets
 */
int rlist_assign_properties (struct rlist *rl,
                             json_t *properties,
                             flux_error_t *errp);

/*  Encode properties to a JSON string which conforms to RFC 20 properties
 *   specification. Caller must free.
 */
char *rlist_properties_encode (const struct rlist *rl);

struct rlist *rlist_from_config (json_t *conf, flux_error_t *errp);

#endif /* !HAVE_SCHED_RLIST_H */
