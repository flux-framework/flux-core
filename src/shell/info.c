/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* job shell info */
#define FLUX_SHELL_PLUGIN_NAME NULL

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/read_all.h"
#include "src/common/librlist/rhwloc.h"
#include "ccan/str/str.h"

#include "internal.h"
#include "info.h"
#include "jobspec.h"

/* Get jobspec from job-info.lookup future and assign.
 * Return 0 on success, -1 on failure (and log error).
 * N.B. assigned values remain valid until future is destroyed.
 */
static int lookup_jobspec_get (flux_future_t *f, char **jobspec)
{
    flux_error_t error;
    const char *J;
    uint32_t userid;
    if (flux_rpc_get_unpack (f, "{s:s}", "J", &J) < 0)
        goto error;
    if (!(*jobspec = flux_unwrap_string (J, true, &userid, &error))) {
        shell_log_error ("failed to unwrap J: %s", error.text);
        return -1;
    }
    if ((uid_t) userid != getuid ()) {
        shell_log_error ("J signing userid %lu != current %lu",
                         (unsigned long) userid,
                         (unsigned long) getuid ());
        return -1;
    }
    return 0;
error:
    shell_log_error ("job-info: %s", future_strerror (f, errno));
    return -1;
}

/* Fetch J from the job-info service.
 * Return future on success or NULL on failure (and log error).
 */
static flux_future_t *lookup_jobspec (flux_t *h, flux_jobid_t jobid)
{
    flux_future_t *f;
    f = flux_rpc_pack (h,
                       "job-info.lookup",
                       FLUX_NODEID_ANY,
                       0,
                       "{s:I s:[s] s:i}",
                       "id", jobid,
                       "keys", "J",
                       "flags", 0);
    if (!f)
        shell_log_error ("error sending job-info request");
    return f;
}

/*  Return 0 if no two jobspec tasks are bound to the same slot label with
 *  slots in `sm`, otherwise log an error and return -1.
 *
 *  The meaning of multiple tasks sharing a slot label, i.e. MPMD across
 *  shared slots, is not yet defined, so reject it rather than run only the
 *  first task, which a future definition could contradict.
 */
static int check_shared_labels (struct shell_info *info, struct slotmap *sm)
{
    const char **labels = info->jobspec->labels;

    for (int i = 0; labels && labels[i]; i++) {
        for (int j = 0; j < i; j++) {
            if (streq (labels[i], labels[j])
                && slotmap_count_slots (sm, labels[i]) > 0) {
                shell_log_error ("multiple tasks bound to slot label %s"
                                 " are not supported",
                                 labels[i]);
                return -1;
            }
        }
    }
    return 0;
}

/*  Create slots in `sm` from the jobspec if R has no slots or the
 *  per-resource shell option is set, and return the number of tasks to
 *  distribute over the slots in `ntasksp`. Set `labelsp` to the slot labels
 *  used to select scheduler assigned slots, or NULL to use all slots.
 */
static int shell_info_create_slots (struct shell_info *info,
                                    struct slotmap *sm,
                                    int *ntasksp,
                                    const char ***labelsp)
{
    struct jobspec *jobspec = info->jobspec;
    int ntasks = jobspec->task_count;
    const char **labels = NULL;
    flux_error_t error;

    if (jobspec->per_resource != NULL) {
        /*  One slot per resource, and per_resource_count tasks per slot.
         */
        if (slotmap_set_slots_per_resource (sm,
                                            jobspec->slot_label,
                                            jobspec->per_resource,
                                            &error) < 0) {
            shell_log_error ("error distributing %d tasks per-%s over R: %s",
                             jobspec->per_resource_count,
                             jobspec->per_resource,
                             error.text);
            return -1;
        }
        ntasks = slotmap_count_slots (sm, NULL) * jobspec->per_resource_count;
    }
    else if (slotmap_has_slots (sm)) {
        /*  Use RFC 20 slots from R with the slot labels of the jobspec
         *  tasks. With multiple tasks, e.g. for "or" slots, the scheduler
         *  chose which labels to assign. If no slots have any of the labels,
         *  slotmap_distribute() uses all slots.
         */
        if (check_shared_labels (info, sm) < 0)
            return -1;
        labels = jobspec->labels;
        if (jobspec->per_slot) {
            int nslots = slotmap_count_slots_labels (sm, labels);
            if (nslots == 0)
                nslots = slotmap_count_slots (sm, NULL);
            ntasks = nslots * jobspec->per_slot;
        }
    }
    else {
        /*  Create one slot per task, each with the cores and gpus of a
         *  jobspec slot, so tasks are placed as by the original shell
         *  task distribution.
         */
        if (slotmap_set_slots_even (sm,
                                    jobspec->slot_label,
                                    ntasks,
                                    jobspec->cores_per_slot,
                                    jobspec->gpus_per_slot,
                                    &error) < 0) {
            shell_log_error ("error creating %d slots in R: %s",
                             ntasks,
                             error.text);
            return -1;
        }
    }
    *ntasksp = ntasks;
    *labelsp = labels;
    return 0;
}

/*  Check that every node in `sm` has at least one task, which the job
 *  shell requires. Log an error and return -1 if not.
 */
static int check_every_node_has_tasks (struct shell_info *info,
                                       struct slotmap *sm,
                                       int ntasks)
{
    for (int i = 0; i < slotmap_total_nodes (sm); i++) {
        struct slotmap_rankinfo ri;
        char *labels;

        if (slotmap_get_nth (sm, i, &ri) == 0 && ri.ntasks > 0)
            continue;
        /*  A node gets no tasks if none of its slots have a slot label of
         *  a jobspec task, while other nodes do.
         */
        if (slotmap_has_slots (sm)
            && info->jobspec->labels
            && slotmap_count_slots_labels (sm, info->jobspec->labels) > 0
            && (labels = slotmap_labels (sm, i))) {
            shell_log_error ("rank %d: no task in jobspec for slot label %s",
                             ri.rank,
                             labels);
            free (labels);
        }
        else
            shell_log_error ("%d tasks cannot be distributed over %d nodes"
                             " with at least one task per node",
                             ntasks,
                             slotmap_total_nodes (sm));
        return -1;
    }
    return 0;
}

/*  Assign the tasks in jobspec to slots in `sm`.
 */
static int shell_info_distribute (struct shell_info *info, struct slotmap *sm)
{
    const char **labels;
    int ntasks;
    flux_error_t error;

    if (shell_info_create_slots (info, sm, &ntasks, &labels) < 0)
        return -1;
    if (slotmap_distribute (sm, labels, ntasks, &error) < 0) {
        shell_log_error ("error distributing %d tasks over R: %s",
                         ntasks,
                         error.text);
        return -1;
    }
    if (check_every_node_has_tasks (info, sm, ntasks) < 0)
        return -1;
    return 0;
}

/*  Return true if the jobspec v1 quirk with uneven distribution of
 *  tasks to nodes applies (#7174). With jobspec v1, if nodes are requested
 *  and tasks do not divide evenly among them (e.g. `flux run -N2 -n3`),
 *  extra slots are requested since jobspec v1 allows only one `node`
 *  resource. In this case, the resources of unused slots are removed
 *  with slotmap_normalize(), so each task has an equal count of cores
 *  and gpus, at the expense of wasting a slot.
 *
 *  Apply this fix as narrowly as possible by ensuring the following:
 *  - jobspec version is 1
 *  - the jobspec did not specify node exclusive allocation
 *  - the jobspec did not specify a per-resource object
 *  - number of nodes allocated > 1
 *  - there are more total slots than tasks
 */
bool shell_info_need_normalize (struct shell_info *info)
{
    struct jobspec *jobspec = info->jobspec;

    return (jobspec->version == 1
            && !jobspec->node_exclusive
            && !jobspec->per_resource
            && jobspec->node_count > 1
            && jobspec->slot_count > info->total_ntasks);
}

/*  Unpack R from a job-info.update-watch response and update the
 *  shell's internal info->R and info->slotmap. If a response can't be
 *  unpacked or slotmap_create() fails, just ignore this response
 *  and let caller decide if the error is fatal.
 *
 *  If jobspec has already been parsed, i.e. this is an update of R,
 *  assign tasks to slots in the new slotmap from the current taskmap.
 */
static int resource_watch_update (struct shell_info *info)
{
    int rc = -1;
    flux_future_t *f = info->R_watch_future;
    json_t *R = NULL;
    struct slotmap *sm = NULL;
    flux_error_t error;

    if (flux_rpc_get_unpack (f, "{s:o}", "R", &R) < 0) {
        shell_log_errno ("error getting R from job-info watch response");
        goto out;
    }
    if (!(sm = slotmap_create (R, &error))) {
        shell_log_error ("error decoding R: %s", error.text);
        goto out;
    }
    if (info->jobspec) {
        if (shell_info_distribute (info, sm) < 0
            || (info->taskmap && slotmap_update_map (sm, info->taskmap) < 0)
            || (shell_info_need_normalize (info)
                && slotmap_normalize (sm,
                                      info->jobspec->cores_per_slot,
                                      info->jobspec->gpus_per_slot) < 0)) {
            shell_log_error ("error applying tasks to updated R");
            slotmap_destroy (sm);
            goto out;
        }
    }
    /*  Swap previous and updated R, slotmap:
     */
    json_decref (info->R);
    info->R = json_incref (R);
    slotmap_destroy (info->slotmap);
    info->slotmap = sm;
    rc = 0;
out:
    flux_future_reset (f);
    return rc;
}

static void R_update_cb (flux_future_t *f, void *arg)
{
    flux_shell_t *shell = arg;

    if (resource_watch_update (shell->info) < 0)
        return;

    /*  Destroy cached shell "info" JSON object otherwise plugins will
     *  not see the updated R
     */
    (void) flux_shell_aux_set (shell, "shell::info", NULL, NULL);

    /* Notify plugins that resources have been updated.
     * (Assume plugins will emit appropriate error messages, so ignore
     *  error from flux_shell_plugstack_call()).
     */
    (void) flux_shell_plugstack_call (shell, "shell.resource-update", NULL);
}

/*  Fetch jobinfo (jobspec, R) from job-info service if not provided on
 *   command line, and parse.
 */
static int shell_init_jobinfo (flux_shell_t *shell, struct shell_info *info)
{
    int rc = -1;
    flux_future_t *f_info = NULL;
    flux_future_t *f_hwloc = NULL;
    const char *xml;
    char *jobspec = NULL;
    json_error_t error;

    /*  fetch hwloc topology from resource module to avoid having to
     *  load from scratch here. The topology XML is then cached for
     *  future shell plugin use.
     */
    if (!(f_hwloc = flux_rpc (shell->h,
                              "resource.topo-get",
                              NULL,
                              FLUX_NODEID_ANY,
                              0)))
        goto out;

    /*  fetch R from job-info service
     */
    if (!(info->R_watch_future = flux_rpc_pack (shell->h,
                                                "job-info.update-watch",
                                                FLUX_NODEID_ANY,
                                                FLUX_RPC_STREAMING,
                                                "{s:I s:s s:i}",
                                                "id", shell->jobid,
                                                "key", "R",
                                                "flags", 0)))
        goto out;

    /*  fetch jobspec (via J) for this job
     */
    if (!(f_info = lookup_jobspec (shell->h, shell->jobid)))
        goto out;

    if (flux_rpc_get (f_hwloc, &xml) < 0
        || !(info->hwloc_xml = strdup (xml))) {
        shell_log_error ("error fetching local hwloc xml");
        if (!(info->hwloc_xml = rhwloc_local_topology_xml (0))) {
            shell_log_error ("error loading local hwloc xml");
            goto out;
        }
    }
    if (lookup_jobspec_get (f_info, &jobspec) < 0) {
        shell_log_error ("error fetching jobspec");
        goto out;
    }

    /*  Synchronously get initial version of R from first job-info
     *  watch response:
     */
    if (resource_watch_update (info) < 0)
        goto out;

    /*  Parse jobspec after R so resource information can be read from R:
     */
    if (!(info->jobspec = jobspec_parse (jobspec, info->slotmap, &error))) {
        shell_log_error ("error parsing jobspec: %s", error.text);
        goto out;
    }
    if (info->jobspec->version != 1) {
        shell_warn ("Unsupported jobspec version: expected 1 got %d",
                    info->jobspec->version);
    }

    /*  Register callback for future R updates:
     */
    if (flux_future_then (info->R_watch_future,
                          -1.,
                          R_update_cb,
                          shell) < 0) {
        shell_log_errno ("error registering R watch callback");
        goto out;
    }
    rc = 0;
out:
    free (jobspec);
    flux_future_destroy (f_hwloc);
    flux_future_destroy (f_info);
    return rc;
}

struct taskmap *create_taskmap (struct shell_info *info)
{
    struct taskmap *map = taskmap_create ();
    if (!map)
        return NULL;
    for (int i = 0; i < info->shell_size; i++) {
        struct slotmap_rankinfo ri;
        if (slotmap_get_nth (info->slotmap, i, &ri) < 0
            || taskmap_append (map, i, 1, ri.ntasks) < 0) {
            shell_log_errno ("taskmap: failed to process rank=%d", i);
            goto error;
        }
    }
    return map;
error:
    taskmap_destroy (map);
    return NULL;
}

int shell_info_set_taskmap (struct shell_info *info,
                            struct taskmap *map)
{
    flux_error_t error;
    const struct idset *taskids;
    struct idset *copy;

    if (!info || !map) {
        errno = EINVAL;
        return -1;
    }
    if (taskmap_unknown (map)) {
        shell_log_error ("invalid taskmap: mapping is unknown");
        return -1;
    }
    if (info->taskmap
        && taskmap_check (info->taskmap, map, &error) < 0) {
        shell_log_error ("invalid taskmap: %s", error.text);
        return -1;
    }
    if (!(taskids = taskmap_taskids (map, info->shell_rank))
        || !(copy = idset_copy (taskids)))
        return -1;

    /* Update slotmap using the new taskmap.
     * Be sure to update the local rankinfo structure, which may have changed.
     */
    if (slotmap_update_map (info->slotmap, map) < 0
        || slotmap_get_nth (info->slotmap,
                            info->shell_rank,
                            &info->rankinfo) < 0) {
        shell_log_error ("unable to update task counts from new taskmap");
        return -1;
    }

    idset_destroy (info->taskids);
    info->taskids = copy;
    taskmap_destroy (info->taskmap);
    info->taskmap = map;
    return 0;
}

struct shell_info *shell_info_create (flux_shell_t *shell)
{
    struct shell_info *info;
    int broker_rank = shell->broker_rank;
    struct taskmap *map = NULL;

    if (!(info = calloc (1, sizeof (*info)))) {
        shell_log_errno ("shell_info_create");
        return NULL;
    }
    info->jobid = shell->jobid;
    if (getenv ("FLUX_EXEC_CONSTRAINED_RESOURCES")) {
        info->constrained_resources = true;
        unsetenv ("FLUX_EXEC_CONSTRAINED_RESOURCES");
    }

    if (shell_init_jobinfo (shell, info) < 0)
        goto error;

    if (shell_info_distribute (info, info->slotmap) < 0)
        goto error;
    if ((info->shell_rank = slotmap_nodeid (info->slotmap, broker_rank)) < 0
        || slotmap_get_nth (info->slotmap,
                            info->shell_rank,
                            &info->rankinfo) < 0) {
        shell_log_error ("error fetching rankinfo for rank %d", broker_rank);
        goto error;
    }
    info->shell_size = slotmap_total_nodes (info->slotmap);
    info->total_ntasks = slotmap_total_ntasks (info->slotmap);

    if (!(map = create_taskmap (info))
        || shell_info_set_taskmap (info, map) < 0) {
        taskmap_destroy (map);
        shell_log_error ("error creating taskmap");
        goto error;
    }
    return info;
error:
    shell_info_destroy (info);
    return NULL;
}

void shell_info_destroy (struct shell_info *info)
{
    if (info) {
        int saved_errno = errno;
        flux_future_destroy (info->R_watch_future);
        json_decref (info->R);
        jobspec_destroy (info->jobspec);
        slotmap_destroy (info->slotmap);
        taskmap_destroy (info->taskmap);
        idset_destroy (info->taskids);
        hostlist_destroy (info->hostlist);
        free (info->hwloc_xml);
        free (info);
        errno = saved_errno;
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
