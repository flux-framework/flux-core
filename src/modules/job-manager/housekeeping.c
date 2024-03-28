/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* housekeeping - clean resources prior to release to the scheduler
 *
 * Purpose:
 *   Resources are released by jobs to housekeeping.  Housekeeping runs
 *   an epilog-like script, then releases resources to the scheduler.
 *   Unlike the epilog, housekeeping is intended to be divorced from the
 *   job, used for admin tasks like configuration management updates.
 *   The job does not remain in CLEANUP state while housekeeping runs,
 *   although the scheduler still thinks resources are allocated to the job.
 *
 * Configuration:
 *   [job-manager.housekeeping]
 *   command = "command arg1 arg2 ..."
 *   release-after = "FSD"
 *
 * Partial release:
 *   The 'release-after' config key enables partial release of resources.
 *   - If unset, resources for a given job are not released until all exec
 *     targets have completed housekeeping.
 *   - If set to "0", resources are released as each exec target completes.
 *   - If set to a nozero duration, a timer starts when the first exec target
 *     for a given job completes.  When the timer expires, resources for all
 *     the completed exec targets are released.  Following that, resources
 *     are released as each target completes.
 *
 * Script credentials:
 *   The housekeeping script runs as the instance owner (e.g. "flux").
 *
 * Script environment:
 *   The environment is derived from the rank 0 broker's environment.
 *   Job-related environment variables are unset.
 *   FLUX_URI points to the local broker.
 *
 * script error handling:
 *   The script wait status is logged at LOG_ERR if it did not exit 0.
 *   Other script errors must be managed by the script itself:
 *   - Standard I/O is discarded.  Use flux-logger(1) if needed.
 *   - The script can run forever.  Use timeout(1) or equivalent as needed.
 *   - No drain on failure.  Use flux-resource(1) to drain nodes if needed.
 *
 * Core scheduled instances:
 *   Note that housekeeping runs after every job even if the job did not
 *   allocate the whole node.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <flux/idset.h>
#include <unistd.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/librlist/rlist.h"
#include "src/common/libhostlist/hostlist.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libjob/idf58.h"
#include "src/common/libsubprocess/client.h"
#include "src/common/libsubprocess/command.h"
#include "ccan/str/str.h"

#include "job.h"
#include "alloc.h"
#include "job-manager.h"
#include "conf.h"

#include "housekeeping.h"

// -1 = never, 0 = immediate, >0 = time in seconds
static const double default_release_after = -1;

struct allocation {
    flux_jobid_t id;
    struct rlist *rl;       // R, diminished each time a subset is released
    struct idset *pending;  // ranks in need of housekeeping
    struct housekeeping *hk;
    flux_watcher_t *timer;
    bool timer_armed;
    bool timer_expired;
    int free_count;         // number of releases
    double t_start;
};

struct exec_target {
    flux_future_t *f;
};

struct housekeeping {
    struct job_manager *ctx;
    flux_cmd_t *cmd; // NULL if not configured
    double release_after;
    zlistx_t *allocations;
    uint32_t size;
    struct exec_target *targets; // array of size entries (indexed by rank)
};

static void allocation_timeout (flux_reactor_t *r,
                                flux_watcher_t *w,
                                int revents,
                                void *arg);

static const char *env_blocklist[] = {
    "FLUX_JOB_ID",
    "FLUX_JOB_SIZE",
    "FLUX_JOB_NNODES",
    "FLUX_JOB_TMPDIR",
    "FLUX_TASK_RANK",
    "FLUX_TASK_LOCAL_ID",
    "FLUX_URI",
    "FLUX_KVS_NAMESPACE",
    "FLUX_PROXY_REMOTE",
    NULL,
};

static void allocation_destroy (struct allocation *a)
{
    if (a) {
        int saved_errno = errno;
        rlist_destroy (a->rl);
        idset_destroy (a->pending);
        flux_watcher_destroy (a->timer);
        free (a);
        errno = saved_errno;
    }
}

// zlistx_destructor_fn footprint
static void allocation_destructor (void **item)
{
    if (item) {
        allocation_destroy (*item);
        *item = NULL;
    }

}

static struct allocation *allocation_create (struct housekeeping *hk,
                                             json_t *R,
                                             flux_jobid_t id)
{
    struct allocation *a;
    flux_reactor_t *r = flux_get_reactor (hk->ctx->h);

    if (!(a = calloc (1, sizeof (*a))))
        return NULL;
    a->hk = hk;
    a->id = id;
    a->t_start = flux_reactor_now (flux_get_reactor (hk->ctx->h));
    if (!(a->rl = rlist_from_json (R, NULL))
        || !(a->pending = rlist_ranks (a->rl))
        || !(a->timer = flux_timer_watcher_create (r,
                                                   0,
                                                   0.,
                                                   allocation_timeout,
                                                   a))) {
        allocation_destroy (a);
        return NULL;
    }
    return a;
}

static struct idset *get_housekept_ranks (struct allocation *a)
{
    struct idset *ranks;
    unsigned int id;

    if (!(ranks = rlist_ranks (a->rl)))
        goto error;
    id = idset_first (ranks);
    while (id != IDSET_INVALID_ID) {
        if (idset_test (a->pending, id))
            if (idset_clear (ranks, id) < 0)
                goto error;
        id = idset_next (ranks, id);
    }
    return ranks;
error:
    idset_destroy (ranks);
    return NULL;
}

/* Release any resources in a->rl associated with ranks that are no longer
 * pending for housekeeping.  Then remove them from a->rl.
 */
static void allocation_release (struct allocation *a)
{
    struct job_manager *ctx = a->hk->ctx;
    struct idset *ranks = NULL;
    struct rlist *rl = NULL;
    json_t *R = NULL;

    if ((ranks = get_housekept_ranks (a)) && idset_count (ranks) == 0) {
        idset_destroy (ranks);
        return; // nothing to do
    }

    if (!ranks
        || !(rl = rlist_copy_ranks (a->rl, ranks))
        || !(R = rlist_to_R (rl))
        || alloc_send_free_request (ctx->alloc, R, a->id) < 0
        || rlist_remove_ranks (a->rl, ranks) < 0) {
        char *s = idset_encode (ranks, IDSET_FLAG_RANGE);
        flux_log (ctx->h,
                  LOG_ERR,
                  "housekeeping error releasing resources for job %s ranks %s",
                  idf58 (a->id),
                  s ? s : "NULL");
        free (s);
    }
    else
        a->free_count++;
    json_decref (R);
    rlist_destroy (rl);
    idset_destroy (ranks);
}

static void allocation_remove (struct allocation *a, void *cursor)
{
    flux_log (a->hk->ctx->h,
              LOG_DEBUG,
              "housekeeping: all resources of %s have been released",
              idf58 (a->id));
    if (!cursor)
        cursor = zlistx_find (a->hk->allocations, a);
    if (!cursor) {
        flux_log (a->hk->ctx->h,
                  LOG_ERR,
                  "housekeeping: internal error removing allocation for %s",
                  idf58 (a->id));
        return;
    }
    zlistx_delete (a->hk->allocations, cursor);
}

static void allocation_timeout (flux_reactor_t *r,
                                flux_watcher_t *w,
                                int revents,
                                void *arg)
{
    struct allocation *a = arg;

    a->timer_expired = true;

    // release the ranks that have completed housekeeping so far
    allocation_release (a);

    // the allocation has been completely released so retire it
    if (rlist_nnodes (a->rl) == 0)
        allocation_remove (a, NULL);
}

/* 'rank' has completed housekeeping.
 */
static void housekeeping_finish_one (struct housekeeping *hk, int rank)
{
    struct allocation *a;

    a = zlistx_first (hk->allocations);
    while (a) {
        if (idset_test (a->pending, rank)) {
            idset_clear (a->pending, rank);

            if (idset_count (a->pending) == 0
                || hk->release_after == 0
                || a->timer_expired) {
                allocation_release (a);
            }
            if (!a->timer_armed && hk->release_after > 0) {
                flux_timer_watcher_reset (a->timer, hk->release_after, 0.);
                flux_watcher_start (a->timer);
                a->timer_armed = true;
            }

            // allocation has been completely released
            if (rlist_nnodes (a->rl) == 0)
                allocation_remove (a, zlistx_cursor (hk->allocations));
        }
        a = zlistx_next (hk->allocations);
    }
}

static void housekeeping_continuation (flux_future_t *f, void *arg)
{
    struct housekeeping *hk = arg;
    flux_t *h = flux_future_get_flux (f);
    int rank = flux_rpc_get_nodeid (f);
    const char *hostname = flux_get_hostbyrank (h, rank);
    int status;

    if (subprocess_rexec_get (f) < 0) {
        if (errno != ENODATA) {
            flux_log (h,
                      LOG_ERR,
                      "housekeeping %s (rank %d): %s",
                      hostname,
                      rank,
                      future_strerror (f, errno));
        }
        flux_future_destroy (f);
        hk->targets[rank].f = NULL;
        housekeeping_finish_one (hk, rank);
        return;
    }
    if (subprocess_rexec_is_finished (f, &status)) {
        if (WIFEXITED (status)) {
            int n = WEXITSTATUS (status);
            flux_log (h,
                      n == 0 ? LOG_INFO : LOG_ERR,
                      "housekeeping %s (rank %d): exit %d",
                      hostname,
                      rank,
                      n);
        }
        else if (WIFSIGNALED (status)) {
            int n = WTERMSIG (status);
            flux_log (h,
                      LOG_ERR,
                      "housekeeping %s (rank %d): %s",
                      hostname,
                      rank,
                      strsignal (n));
        }
    }
    flux_future_reset (f);
}

static int housekeeping_start_one (struct housekeeping *hk, int rank)
{
    flux_future_t *f;
    int flags = 0;

    if (rank >= hk->size)
        return -1;
    if (hk->targets[rank].f != NULL) // in progress already
        return 0;
    if (!(f = subprocess_rexec (hk->ctx->h,
                                "rexec",
                                rank,
                                hk->cmd,
                                flags))
        || flux_future_then (f, -1., housekeeping_continuation, hk) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    hk->targets[rank].f = f;
    return 0;
}

int housekeeping_start (struct housekeeping *hk,
                        json_t *R,
                        flux_jobid_t id)
{
    flux_t *h = hk->ctx->h;
    struct allocation *a;
    unsigned int rank;
    void *list_handle;

    /* Housekeeping is not configured
     */
    if (!hk->cmd)
        goto skip;

    /* Create the 'allocation' and put it in our list.
     */
    if (!(a = allocation_create (hk, R, id))
        || !(list_handle = zlistx_insert (hk->allocations, a, false))) {
        flux_log (h,
                  LOG_ERR,
                  "housekeeping: %s error saving alloc object (skipping)",
                  idf58 (id));
        allocation_destroy (a);
        goto skip;
    }

    /* Iterate over the ranks in the allocation and start housekeeping on
     * each rank, unless already running.  Continuations for the remote
     * execution will find allocations remove rank from a->pending and return
     * resources to the scheduler.
     */
    rank = idset_first (a->pending);
    while (rank != IDSET_INVALID_ID) {
        if (housekeeping_start_one (hk, rank) < 0) {
            flux_log_error (h, "error starting housekeeping on rank %d", rank);
            idset_clear (a->pending, rank);
        }
        rank = idset_next (a->pending, rank);
    }
    if (idset_count (a->pending) == 0) {
        zlistx_delete (hk->allocations, list_handle);
        goto skip;
    }
    return 0;
skip:
    return alloc_send_free_request (hk->ctx->alloc, R, id);
}

/* We need a revision to RFC 27 to support partial allocations in the
 * hello response payload.  For now, just destroy any allocation record
 * that has been partially released and let the scheduler assume any resources
 * currently running housekeeping are "free".  Same deal if the job has
 * been purged or if we drop the response message.
 */
int housekeeping_hello_respond (struct housekeeping *hk, const flux_msg_t *msg)
{
    struct allocation *a;
    struct job *job;

    a = zlistx_first (hk->allocations);
    while (a) {
        if (a->free_count > 0
            || (!(job = zhashx_lookup (hk->ctx->inactive_jobs, &a->id))
                && !(job = zhashx_lookup (hk->ctx->active_jobs, &a->id)))
            || flux_respond_pack (hk->ctx->h,
                                  msg,
                                  "{s:I s:I s:I s:f}",
                                  "id", job->id,
                                  "priority", job->priority,
                                  "userid", (json_int_t) job->userid,
                                  "t_submit", job->t_submit) < 0) {
            struct hostlist *hl = rlist_nodelist (a->rl);
            char *hosts = hostlist_encode (hl);
            json_t *R = rlist_to_R (a->rl);

            flux_log (hk->ctx->h,
                      LOG_ERR,
                      "housekeeping: WARNING still running on %s of %s"
                      " at scheduler restart.  Jobs may be allowed to run"
                      " there before housekeeping is complete.",
                      hosts ? hosts : "some nodes",
                      idf58 (a->id));

            // delete the allocation to avoid sending frees later
            zlistx_delete (hk->allocations, zlistx_cursor (hk->allocations));

            json_decref (R);
            free (hosts);
            hostlist_destroy (hl);
        }
        a = zlistx_next (hk->allocations);
    }
    return 0;
}

static flux_cmd_t *create_cmd (const char *cmdline,
                               const char **blocklist)
{
    char *argz = NULL;
    size_t argz_len = 0;
    int argc;
    char **argv = NULL;
    flux_cmd_t *cmd = NULL;

    if (argz_create_sep (cmdline, ' ', &argz, &argz_len) < 0
        || (argc = argz_count (argz, argz_len)) == 0
        || !(argv = calloc (argc + 1, sizeof (argv[0]))))
        goto done;
    argz_extract (argz, argz_len, argv);
    if (!(cmd = flux_cmd_create (argc, argv, environ)))
        goto done;
    if (blocklist) {
        for (int i = 0; blocklist[i] != NULL; i++)
            flux_cmd_unsetenv (cmd, blocklist[i]);
    }

done:
    free (argz);
    free (argv);
    return cmd;
}

static int housekeeping_parse_config (const flux_conf_t *conf,
                                      flux_error_t *error,
                                      void *arg)
{
    struct housekeeping *hk = arg;
    flux_error_t e;
    const char *cmdline = NULL;
    const char *release_after = NULL;
    flux_cmd_t *cmd = NULL;

    if (flux_conf_unpack (conf,
                          &e,
                          "{s?{s?{s?s s?s !}}}",
                          "job-manager",
                            "housekeeping",
                              "command", &cmdline,
                              "release-after", &release_after) < 0) {
        return errprintf (error,
                          "job-manager.housekeeping.command: %s",
                          e.text);
    }
    if (release_after) {
        if (fsd_parse_duration (release_after, &hk->release_after) < 0)
            return errprintf (error,
                              "job-manager.housekeeping.release-after"
                              " FSD parse error");
    }
    if (cmdline && !(cmd = create_cmd (cmdline, env_blocklist)))
        return errprintf (error, "error creating housekeeping command object");
    flux_cmd_destroy (hk->cmd);
    hk->cmd = cmd;
    flux_log (hk->ctx->h,
              LOG_DEBUG,
              "housekeeping is %sconfigured",
              hk->cmd ? "" : "not ");
    return 1; // allow dynamic changes
}

void housekeeping_ctx_destroy (struct housekeeping *hk)
{
    if (hk) {
        int saved_errno = errno;
        conf_unregister_callback (hk->ctx->conf, housekeeping_parse_config);
        flux_cmd_destroy (hk->cmd);
        zlistx_destroy (&hk->allocations);
        if (hk->targets) {
            for (int i = 0; i < hk->size; i++)
                flux_future_destroy (hk->targets[i].f);
            free (hk->targets);
        }
        free (hk);
        errno = saved_errno;
    }
}

struct housekeeping *housekeeping_ctx_create (struct job_manager *ctx)
{
    struct housekeeping *hk;
    flux_error_t error;

    if (!(hk = calloc (1, sizeof (*hk))))
        return NULL;
    hk->ctx = ctx;
    hk->release_after = default_release_after;
    if (flux_get_size (ctx->h, &hk->size) < 0)
        goto error;
    if (!(hk->targets = calloc (hk->size, sizeof (hk->targets[0]))))
        goto error;
    if (!(hk->allocations = zlistx_new ())) {
        errno = ENOMEM;
        goto error;
    }
    zlistx_set_destructor (hk->allocations, allocation_destructor);
    if (conf_register_callback (ctx->conf,
                                &error,
                                housekeeping_parse_config,
                                hk) < 0) {
        flux_log (ctx->h, LOG_ERR, "%s", error.text);
        goto error;
    }
    return hk;
error:
    housekeeping_ctx_destroy (hk);
    return NULL;
}

// vi:ts=4 sw=4 expandtab
