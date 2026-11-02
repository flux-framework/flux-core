/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* slotmap - map job tasks to RFC 20 task slots
 *
 * A slotmap holds the resources of each execution target in Rv1 and
 * the task slots on each target. Slots are loaded from the optional
 * RFC 20 R_lite "slots" key if the scheduler provided it. Otherwise,
 * the caller creates slots with slotmap_set_slots_even() or
 * slotmap_set_slots_per_resource().
 *
 * Tasks are then assigned to slots with slotmap_distribute(). Thus R
 * with and without slots share the same task distribution code.
 */

#ifndef SHELL_SLOTMAP_H
#define SHELL_SLOTMAP_H

#include <stdbool.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/taskmap.h>

struct slotmap_rankinfo {
    int nodeid;               /* This rank's nodeid within the job       */
    int rank;                 /* The broker rank                         */
    int ntasks;               /* Number of tasks assigned to this rank   */
    int ncores;               /* Number of cores assigned to this rank   */
};

/* Create a slotmap from Rv1 object `R`. If R_lite entries contain
 * RFC 20 slots, validate and load them.
 */
struct slotmap *slotmap_create (json_t *R, flux_error_t *errp);

void slotmap_destroy (struct slotmap *sm);

/* Return true if the current slots were loaded from R.
 */
bool slotmap_has_slots (struct slotmap *sm);

int slotmap_total_nodes (struct slotmap *sm);
int slotmap_total_cores (struct slotmap *sm);
int slotmap_total_gpus (struct slotmap *sm);
int slotmap_total_ntasks (struct slotmap *sm);

/* Return R nslots if set, otherwise the number of slots in R.
 * Return 0 if neither is available.
 */
int slotmap_total_slots (struct slotmap *sm);

/* Return the current number of slots with `label`, or of all slots
 * if `label` is NULL.
 */
int slotmap_count_slots (struct slotmap *sm, const char *label);

/* Return the current number of slots with any label in the NULL
 * terminated array `labels`, or of all slots if `labels` is NULL.
 */
int slotmap_count_slots_labels (struct slotmap *sm, const char **labels);

/* Return the nodeid of broker rank `rank`, or -1 with errno set to
 * ENOENT if `rank` is not in R.
 */
int slotmap_nodeid (struct slotmap *sm, int rank);

/* Replace all slots with `nslots` slots of `cores_per_slot` cores and
 * up to `gpus_per_slot` GPUs. If cores_per_slot <= 0, use total cores
 * divided by nslots. Place slots round-robin, ranks with the most cores
 * first. If the slots do not fit, oversubscribe the ranks.
 */
int slotmap_set_slots_even (struct slotmap *sm,
                            const char *label,
                            int nslots,
                            int cores_per_slot,
                            int gpus_per_slot,
                            flux_error_t *errp);

/* Replace all slots with one slot per resource of `type`, where type is
 * "node" or "core".
 */
int slotmap_set_slots_per_resource (struct slotmap *sm,
                                    const char *label,
                                    const char *type,
                                    flux_error_t *errp);

/* Assign `ntasks` tasks to slots with any label in the NULL terminated
 * array `labels`, or to all slots if `labels` is NULL or no slots have
 * any of the labels. Assign tasks round-robin, ranks with the most slots
 * first, one task per slot. If more tasks than slots remain,
 * oversubscribe the slots in the same order.
 */
int slotmap_distribute (struct slotmap *sm,
                        const char **labels,
                        int ntasks,
                        flux_error_t *errp);

/* Update task counts from `map`. The tasks of each rank are divided
 * evenly among the slots used by the last slotmap_distribute().
 */
int slotmap_update_map (struct slotmap *sm, const struct taskmap *map);

/* Remove unused resources from each rank. If slots were loaded from R,
 * keep the resources of the slots with tasks. Otherwise, keep
 * `cores_per_task` cores and `gpus_per_task` gpus per task, with the
 * lowest ids.
 */
int slotmap_normalize (struct slotmap *sm,
                       int cores_per_task,
                       int gpus_per_task);

int slotmap_get_nth (struct slotmap *sm,
                     int nodeid,
                     struct slotmap_rankinfo *ri);

/* Return the cores or gpus of `nodeid` as an RFC 22 idset string.
 * Caller must free the result.
 */
char *slotmap_cores (struct slotmap *sm, int nodeid);
char *slotmap_gpus (struct slotmap *sm, int nodeid);

/* Return an array of slot objects for `nodeid`, each with keys "label",
 * "ntasks", "cores", and "gpus". Caller must json_decref() the result.
 */
json_t *slotmap_slots_json (struct slotmap *sm, int nodeid);

#endif /* !SHELL_SLOTMAP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
