/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_TASKMAP_H
#define _UTIL_TASKMAP_H

#include <flux/core.h>
#include <flux/idset.h>

#ifdef __cplusplus
extern "C" {
#endif

enum taskmap_flags {
    TASKMAP_ENCODE_WRAPPED = 1,      /* Encode as RFC 34 wrapped object      */
    TASKMAP_ENCODE_PMI =     1 << 1, /* Encode as PMI-1 PMI_process_mapping  */
    TASKMAP_ENCODE_RAW =     1 << 2, /* Encode as semicolon-delimited taskids*/
    TASKMAP_ENCODE_RAW_DERANGED = 1 << 3, /* Encode as raw without ranges    */
};

/*  Create an empty taskmap
 *  Returns taskmap on success, NULL on failure with errno set.
 */
struct taskmap *taskmap_create ();

/*  Destroy a taskmap
 */
void taskmap_destroy (struct taskmap *map);

/*  Append a block of tasks to a taskmap starting at 'nodeid', for 'nnodes'
 *   with 'ppn' tasks per node.
 *  Returns 0 on success, -1 on failure with errno set.
 */
int taskmap_append (struct taskmap *map, int nodeid, int nnodes, int ppn);

/*  Decode string 'map' into taskmap object.
 *  The string may be a JSON array, RFC 34 wrapped object, a mapping
 *   encoded in PMI-1 PMI_process_mapping form described in RFC 13, or
 *   a raw, semicolon-delimited list of taskids.
 *  Returns taskmap on success, or NULL on error with error string in 'errp'.
 */
struct taskmap *taskmap_decode (const char *map, flux_error_t *errp);

/*  Encode taskmap 'map' to a string, which the caller must free.
 *  'flags' may indicate
 *    TASKMAP_ENCODE_WRAPPED to create an RFC 34 wrapped taskmap object.
 *    TASKMAP_ENCODE_PMI to create a PMI-1 PMI_process_mapping encoding
 *    TASKMAP_ENCODE_RAW to create a semicolon-delimited list of taskids
 *  The default is to encode as a JSON array.
 *  Returns string on success, or NULL on failure with errno set.
 */
char *taskmap_encode (const struct taskmap *map, int flags);

/*  Return true if the task mapping is unknown.
 */
bool taskmap_unknown (const struct taskmap *map);

/*  Return an idset of taskids encoded in 'map' for nodeid 'nodeid'.
 *  Returns an idset on success, which may only be valid until the next
 *   call to taskmap_taskids(), caller should use idset_copy() if necessary.
 *  Returns NULL on error with errno set.
 */
const struct idset *taskmap_taskids (const struct taskmap *map, int nodeid);

/*  Return the nodeid which contains task id 'taskid' in taskmap 'map'.
 *  Returns the nodeid or -1 on failure with errno set.
 */
int taskmap_nodeid (const struct taskmap *map, int taskid);

/*  Return the total number of tasks assigned to node 'nodeid' in 'map'.
 *  Returns a task count or -1 on failure with errno set.
 */
int taskmap_ntasks (const struct taskmap *map, int nodeid);

/*  Return the total number of nodes in 'map'.
 *  Returns a count of nodes on success, -1 on failure with errno set.
 */
int taskmap_nnodes (const struct taskmap *map);

/*  Return the total number of tasks in 'map'.
 *  Returns a count of tasks on success, -1 on failure with errno set.
 */
int taskmap_total_ntasks (const struct taskmap *map);

/*  Check if 'a' and 'b' taskmaps are compatible, i.e. they
 *  have equivalent numbers of total tasks and total nodes.
 *
 *  Returns 0 on success, -1 with error message in 'errp' on failure.
 */
int taskmap_check (const struct taskmap *a,
                   const struct taskmap *b,
                   flux_error_t *errp);

#ifdef __cplusplus
}
#endif

#endif /* !_UTIL_TASKMAP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
