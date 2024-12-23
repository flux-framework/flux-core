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
    if (flux_rpc_get_unpack (f, "{s:s}", "J", &J) < 0)
        goto error;
    if (!(*jobspec = flux_unwrap_string (J, true, NULL, &error))) {
        shell_log_error ("failed to unwrap J: %s", error.text);
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

/*  Unpack R from a job-info.update-watch response and update the
 *  shell's internal info->R and info->rcalc. If a response can't be
 *  unpacked or rcalc_create_json() fails, just ignore this response
 *  and let caller decide if the error is fatal.
 */
static int resource_watch_update (struct shell_info *info)
{
    int rc = -1;
    flux_future_t *f = info->R_watch_future;
    json_t *R = NULL;
    rcalc_t *rcalc = NULL;

    if (flux_rpc_get_unpack (f, "{s:o}", "R", &R) < 0) {
        shell_log_errno ("error getting R from job-info watch response");
        goto out;
    }
    if (!(rcalc = rcalc_create_json (R))) {
        shell_log_error ("error decoding R");
        goto out;
    }
    /*  Swap previous and updated R, rcalc:
     */
    json_decref (info->R);
    info->R = json_incref (R);
    rcalc_destroy (info->rcalc);
    info->rcalc = rcalc;
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
    if (!(info->jobspec = jobspec_parse (jobspec, &error))) {
        shell_log_error ("error parsing jobspec: %s", error.text);
        goto out;
    }

    /*  Synchronously get initial version of R from first job-info
     *  watch response:
     */
    if (resource_watch_update (info) < 0)
        goto out;

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

static int get_per_resource_option (struct jobspec *jobspec,
                                    const char **typep,
                                    int *countp)
{
    json_error_t err;
    json_t *o = NULL;

    if (!(o = json_object_get (jobspec->options, "per-resource")))
        return 0;
    *countp = 1;
    if (json_unpack_ex (o, &err, 0,
                        "{s:s s?i}",
                        "type", typep,
                        "count", countp) < 0)
        return shell_log_errn (0, "invalid per-resource spec: %s", err.text);
    return 0;
}

struct taskmap *create_taskmap (struct shell_info *info)
{
    struct taskmap *map = taskmap_create ();
    if (!map)
        return NULL;
    for (int i = 0; i < info->shell_size; i++) {
        struct rcalc_rankinfo ri;
        if (rcalc_get_nth (info->rcalc, i, &ri) < 0
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
    idset_destroy (info->taskids);
    info->taskids = copy;
    taskmap_destroy (info->taskmap);
    info->taskmap = map;
    return 0;
}

struct shell_info *shell_info_create (flux_shell_t *shell)
{
    struct shell_info *info;
    const char *per_resource = NULL;
    int per_resource_count = -1;
    int broker_rank = shell->broker_rank;
    struct taskmap *map = NULL;

    if (!(info = calloc (1, sizeof (*info)))) {
        shell_log_errno ("shell_info_create");
        return NULL;
    }
    info->jobid = shell->jobid;

    if (shell_init_jobinfo (shell, info) < 0)
        goto error;

    if (get_per_resource_option (info->jobspec,
                                 &per_resource,
                                 &per_resource_count) < 0)
        goto error;

    if (per_resource != NULL) {
        if (rcalc_distribute_per_resource (info->rcalc,
                                           per_resource,
                                           per_resource_count) < 0) {
            shell_log_error ("error distributing %d tasks per-%s over R",
                             per_resource_count, per_resource);
            goto error;
        }
    }
    else if (rcalc_distribute (info->rcalc,
                               info->jobspec->task_count,
                               info->jobspec->cores_per_slot) < 0) {
        shell_log_error ("error distributing %d tasks over R",
                         info->jobspec->task_count);
        goto error;
    }
    if (rcalc_get_rankinfo (info->rcalc, broker_rank, &info->rankinfo) < 0) {
        shell_log_error ("error fetching rankinfo for rank %d", broker_rank);
        goto error;
    }
    info->shell_size = rcalc_total_nodes (info->rcalc);
    info->shell_rank = info->rankinfo.nodeid;
    info->total_ntasks = rcalc_total_ntasks (info->rcalc);

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
        rcalc_destroy (info->rcalc);
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
