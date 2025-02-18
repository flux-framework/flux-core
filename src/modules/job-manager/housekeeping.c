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
 *   Unlike the job manager epilog, housekeeping runs after the job, which
 *   is allowed to exit CLEANUP when resources are handed over to housekeeping.
 *   The scheduler still thinks resources are allocated to the job.
 *
 * Configuration:
 *   [job-manager.housekeeping]
 *   #command = ["command", "arg1", "arg2", ...]
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
 *   On a real system, "command" is configured to "imp run housekeeping",
 *   and the IMP is configured to launch the flux-housekeeping systemd
 *   service as root.
 *
 * Script environment:
 *   FLUX_JOB_ID - the job whose resources are running housekeeping
 *   FLUX_JOB_USERID - the UID of the job's owner
 *   FLUX_URI - the URI of the local flux broker
 *   The IMP must be configured to explicitly allow FLUX_* to pass through.
 *
 * Script error handling:
 *   If housekeeping fails on a node or set of nodes, this is logged to
 *   the flux circular buffer at LOG_ERR.
 *   Stdout is logged at LOG_INFO and stderr at LOG_ERR.
 *
 * Error handling under systemd:
 *   When using systemd, any output is captured by the systemd journal on
 *   the remote node, accessed with 'journalctl -u flux-housekeeping@*'.
 *
 *   If the housekeeping script fails, the systemd unit file automatically
 *   drains the node.
 *
 * Core scheduled instances:
 *   Note that housekeeping runs after every job even if the job did not
 *   allocate the whole node.
 *
 * Job manager module stats:
 *   'flux module stats job-manager | jq .housekeeping' returns the following:
 *     {"running":o}
 *   "running" is a dictionary of jobids (f58) for jobs currently
 *   running housekeeping.  Each job object consists of:
 *     {"pending":s "allocated":s, "t_start":f}
 *   where
 *     pending: set of ranks on which housekeeping is needed/active
 *     allocated: set of ranks still allocated by housekeeping
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <flux/idset.h>
#include <unistd.h>
#include <signal.h>
#include <jansson.h>
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
#include "src/common/libsubprocess/bulk-exec.h"
#include "src/common/libsubprocess/command.h"
#include "ccan/str/str.h"

#include "job.h"
#include "alloc.h"
#include "job-manager.h"
#include "conf.h"

#include "housekeeping.h"

extern char **environ;

// -1 = never, 0 = immediate, >0 = time in seconds
static const double default_release_after = -1;

struct allocation {
    flux_jobid_t id;
    struct rlist *rl;       // R, diminished each time a subset is released
    struct idset *pending;  // ranks in need of housekeeping
    struct idset *free;     // ranks that have been released to the scheduler
    struct housekeeping *hk;
    flux_watcher_t *timer;
    bool timer_armed;
    bool timer_expired;
    double t_start;
    struct bulk_exec *bulk_exec;
    void *list_handle;
};

struct housekeeping {
    struct job_manager *ctx;
    flux_cmd_t *cmd; // NULL if not configured
    double release_after;
    char *imp_path;
    zlistx_t *allocations;
    flux_msg_handler_t **handlers;
};

static struct bulk_exec_ops bulk_ops;

static void allocation_timeout (flux_reactor_t *r,
                                flux_watcher_t *w,
                                int revents,
                                void *arg);

static void allocation_destroy (struct allocation *a)
{
    if (a) {
        int saved_errno = errno;
        rlist_destroy (a->rl);
        idset_destroy (a->pending);
        idset_destroy (a->free);
        flux_watcher_destroy (a->timer);
        bulk_exec_destroy (a->bulk_exec);
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

static int update_cmd_env (flux_cmd_t *cmd, flux_jobid_t id, uint32_t userid)
{
    if (flux_cmd_setenvf (cmd, 1, "FLUX_JOB_ID", "%ju", (uintmax_t)id) < 0
        || flux_cmd_setenvf (cmd, 1, "FLUX_JOB_USERID", "%u", userid) < 0)
        return -1;
    return 0;
}

static struct allocation *allocation_create (struct housekeeping *hk,
                                             json_t *R,
                                             flux_jobid_t id,
                                             uint32_t userid)
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
        || !(a->free = idset_create (idset_universe_size (a->pending), 0))
        || !(a->timer = flux_timer_watcher_create (r,
                                                   0,
                                                   0.,
                                                   allocation_timeout,
                                                   a))
        || !(a->bulk_exec = bulk_exec_create (&bulk_ops,
                                              "rexec",
                                              id,
                                              "housekeeping",
                                               a))
        || update_cmd_env (hk->cmd, id, userid) < 0
        || bulk_exec_push_cmd (a->bulk_exec, a->pending, hk->cmd, 0) < 0) {
        allocation_destroy (a);
        return NULL;
    }
    return a;
}

/*  Return the set of ranks in the remaining resource set (a->rl) which are
 *  not still pending housekeeping (a->pending). That is:
 *
 *   ranks (a->rl) -= a->pending
 *
 */
static struct idset *get_housekept_ranks (struct allocation *a)
{
    struct idset *ranks;

    if (!(ranks = rlist_ranks (a->rl)))
        goto error;
    if (idset_subtract (ranks, a->pending) < 0)
        goto error;
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
    bool final = false;

    if ((ranks = get_housekept_ranks (a)) && idset_count (ranks) == 0) {
        idset_destroy (ranks);
        return; // nothing to do
    }
    if (idset_empty (a->pending))
        final = true;

    if (!ranks
        || !(rl = rlist_copy_ranks (a->rl, ranks))
        || !(R = rlist_to_R (rl))
        || alloc_send_free_request (ctx->alloc, R, a->id, final) < 0
        || rlist_remove_ranks (a->rl, ranks) < 0
        || idset_add (a->free, ranks) < 0) {
        char *s = idset_encode (ranks, IDSET_FLAG_RANGE);
        flux_log (ctx->h,
                  LOG_ERR,
                  "housekeeping error releasing resources for job %s ranks %s",
                  idf58 (a->id),
                  s ? s : "NULL");
        free (s);
    }
    json_decref (R);
    rlist_destroy (rl);
    idset_destroy (ranks);
}

static void allocation_remove (struct allocation *a)
{
    if (!a->list_handle
        || zlistx_delete (a->hk->allocations, a->list_handle) < 0) {
        flux_log (a->hk->ctx->h,
                  LOG_CRIT,
                  "housekeeping: internal error removing allocation for %s",
                  idf58 (a->id));
    }
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

    /* Note: All resources will never be released under the timeout
     * because completion of housekeeping on the final rank will
     * always release all resources immediately instead of waiting
     * for the timer. Therefore, there is no need to check if
     * rlist_rnodes (a->rl) is zero here (it never will be).
     */
}

/* 'rank' has completed housekeeping.
 */
static bool housekeeping_finish_one (struct allocation *a, int rank)
{
    if (!idset_test (a->pending, rank))
        return false;
    idset_clear (a->pending, rank);

    if (idset_count (a->pending) == 0
        || a->hk->release_after == 0
        || a->timer_expired) {
        allocation_release (a);
    }
    if (!a->timer_armed && a->hk->release_after > 0) {
        flux_timer_watcher_reset (a->timer, a->hk->release_after, 0.);
        flux_watcher_start (a->timer);
        a->timer_armed = true;
    }
    return true;
}

static void set_failed_reason (const char **s, const char *reason)
{
    if (!*s)
        *s = reason;
    else if (!streq (*s, reason))
        *s = "multiple failure modes";
}

static void bulk_start (struct bulk_exec *bulk_exec, void *arg)
{
    struct allocation *a = arg;
    flux_t *h = a->hk->ctx->h;

    flux_log (h, LOG_DEBUG, "housekeeping: %s started", idf58 (a->id));
}

static void bulk_exit (struct bulk_exec *bulk_exec,
                       void *arg,
                       const struct idset *ids)
{
    struct allocation *a = arg;
    flux_t *h = a->hk->ctx->h;
    unsigned int rank;
    struct idset *failed_ranks = NULL;
    char *failed_ranks_str = NULL;
    char *failed_hosts = NULL;
    const char *failed_reason = NULL;

    rank = idset_first (ids);
    while (rank != IDSET_INVALID_ID) {
        if (housekeeping_finish_one (a, rank)) {
            flux_subprocess_t *p = bulk_exec_get_subprocess (bulk_exec, rank);
            bool fail = false;
            int n;
            if ((n = flux_subprocess_signaled (p)) > 0) {
                fail = true;
                set_failed_reason (&failed_reason, strsignal (n));
            }
            else {
                n = flux_subprocess_exit_code (p);
                if (n != 0) {
                    fail = true;
                    set_failed_reason (&failed_reason, "nonzero exit code");
                }
            }
            if (fail) {
                if (!failed_ranks)
                    failed_ranks = idset_create (0, IDSET_FLAG_AUTOGROW);
                idset_set (failed_ranks, rank);
            }
        }
        rank = idset_next (ids, rank);
    }
    // log a consolidated error message for potentially multiple ranks
    if (failed_ranks
        && (failed_ranks_str = idset_encode (failed_ranks, IDSET_FLAG_RANGE))
        && (failed_hosts = flux_hostmap_lookup (h, failed_ranks_str, NULL))
        && failed_reason) {
        flux_log (h,
                  LOG_ERR,
                  "housekeeping: %s (rank %s) %s: %s",
                  failed_hosts,
                  failed_ranks_str,
                  idf58 (a->id),
                  failed_reason);

    }
    idset_destroy (failed_ranks);
    free (failed_ranks_str);
    free (failed_hosts);
}

static void bulk_complete (struct bulk_exec *bulk_exec, void *arg)
{
    struct allocation *a = arg;
    flux_t *h = a->hk->ctx->h;

    flux_log (h, LOG_DEBUG, "housekeeping: %s complete", idf58 (a->id));
    allocation_remove (a);
}

static void bulk_output (struct bulk_exec *bulk_exec,
                         flux_subprocess_t *p,
                         const char *stream,
                         const char *data,
                         int data_len,
                         void *arg)
{
    struct allocation *a = arg;
    flux_t *h = a->hk->ctx->h;
    int rank = flux_subprocess_rank (p);

    flux_log (h,
              streq (stream, "stderr") ? LOG_ERR : LOG_INFO,
              "housekeeping: %s (rank %d) %s: %.*s",
              flux_get_hostbyrank (h, rank),
              rank,
              idf58 (a->id),
              data_len,
              data);
}

static void bulk_error (struct bulk_exec *bulk_exec,
                        flux_subprocess_t *p,
                        void *arg)
{
    struct allocation *a = arg;
    flux_t *h = a->hk->ctx->h;
    int rank = flux_subprocess_rank (p);
    const char *hostname = flux_get_hostbyrank (h, rank);
    const char *error = flux_subprocess_fail_error (p);

    flux_log (h,
              LOG_ERR,
              "housekeeping: %s (rank %d) %s: %s",
              hostname,
              rank,
              idf58 (a->id),
              error);

    housekeeping_finish_one (a, rank);
}

int housekeeping_start (struct housekeeping *hk,
                        json_t *R,
                        flux_jobid_t id,
                        uint32_t userid)
{
    flux_t *h = hk->ctx->h;
    struct allocation *a;

    /* Housekeeping is not configured
     */
    if (!hk->cmd)
        goto skip;

    /* Create and start the 'allocation' and put it in our list.
     * N.B. bulk_exec_start() starts watchers but does not send RPCs.
     */
    if (!(a = allocation_create (hk, R, id, userid))
        || bulk_exec_start (h, a->bulk_exec) < 0
        || !(a->list_handle = zlistx_insert (hk->allocations, a, false))) {
        flux_log (h,
                  LOG_ERR,
                  "housekeeping: %s error creating alloc object"
                  " - returning resources to the scheduler",
                  idf58 (id));
        allocation_destroy (a);
        goto skip;
    }
    return 0;
skip:
    return alloc_send_free_request (hk->ctx->alloc, R, id, true);
}

static int set_idset_string (json_t *obj, const char *key, struct idset *ids)
{
    char *s;
    json_t *o = NULL;

    if (!(s = idset_encode (ids, IDSET_FLAG_RANGE))
        || !(o = json_string (s))
        || json_object_set_new (obj, key, o) < 0) {
        json_decref (o);
        free (s);
        return -1;
    }
    free (s);
    return 0;
}

static int housekeeping_hello_respond_one (struct housekeeping *hk,
                                           const flux_msg_t *msg,
                                           struct allocation *a,
                                           bool partial_ok,
                                           flux_error_t *error)
{
    struct job *job;
    json_t *payload = NULL;

    if (!idset_empty (a->free) && !partial_ok) {
        errprintf (error,
                   "scheduler does not support restart with partially"
                   " released resources");
        return -1;
    }
    if (!(job = zhashx_lookup (hk->ctx->inactive_jobs, &a->id))
        && !(job = zhashx_lookup (hk->ctx->active_jobs, &a->id))) {
        errprintf (error, "the job could not be looked up during RFC 27 hello");
        return -1;
    }
    if (!(payload = json_pack ("{s:I s:I s:I s:f}",
                               "id", job->id,
                               "priority", job->priority,
                               "userid", (json_int_t)job->userid,
                               "t_submit", job->t_submit)))
        goto error;
    if (!idset_empty (a->free)) {
        if (set_idset_string (payload, "free", a->free) < 0)
            goto error;
    }
    if (flux_respond_pack (hk->ctx->h, msg, "O", payload) < 0)
        goto error;
    json_decref (payload);
    return 0;
error:
    errprintf (error,
               "failed to send scheduler HELLO handshake: %s",
               strerror (errno));
    json_decref (payload);
    return -1;
}

static void kill_continuation (flux_future_t *f, void *arg)
{
    struct housekeeping *hk = arg;

    if (flux_future_get (f, NULL) < 0)
        flux_log (hk->ctx->h, LOG_ERR, "kill: %s", future_strerror (f, errno));
    flux_future_destroy (f);
}

/* Participate in the scheduler hello protocol, where the scheduler is informed
 * of resources that are already allocated.  Since partial release is not yet
 * supported in the hello protocol, for now, we must let go of any partial
 * allocations.  Send remaining housekeeping tasks a SIGTERM, log an error,
 * and delete the allocation.
 */
int housekeeping_hello_respond (struct housekeeping *hk,
                                const flux_msg_t *msg,
                                bool partial_ok)
{
    struct allocation *a;
    flux_error_t error;

    a = zlistx_first (hk->allocations);
    while (a) {
        if (housekeeping_hello_respond_one (hk,
                                            msg,
                                            a,
                                            partial_ok,
                                            &error) < 0) {
            char *ranks;
            char *hosts = NULL;
            flux_future_t *f;

            if ((ranks = idset_encode (a->pending, IDSET_FLAG_RANGE)))
                hosts = flux_hostmap_lookup (hk->ctx->h, ranks, NULL);
            flux_log (hk->ctx->h,
                      LOG_ERR,
                      "housekeeping: %s (rank %s) from %s will be terminated"
                      " because %s",
                      hosts ? hosts : "?",
                      ranks ? ranks : "?",
                      idf58 (a->id),
                      error.text);
            free (hosts);
            free (ranks);

            f = bulk_exec_kill (a->bulk_exec, NULL, SIGTERM);
            if (flux_future_then (f, -1, kill_continuation, hk) < 0)
                flux_future_destroy (f);

            // delete the allocation to avoid sending frees later
            allocation_remove (a);
        }
        a = zlistx_next (hk->allocations);
    }
    return 0;
}

static json_t *housekeeping_get_stats_job (struct allocation *a)
{
    struct idset *ranks;
    char *s = NULL;
    char *p = NULL;
    json_t *job = NULL;

    if (!(ranks = rlist_ranks (a->rl))
        || !(p = idset_encode (ranks, IDSET_FLAG_RANGE))
        || !(s = idset_encode (a->pending, IDSET_FLAG_RANGE)))
        goto out;
    job = json_pack ("{s:f s:s s:s}",
                     "t_start", a->t_start,
                     "pending", s,
                     "allocated", p);
out:
    idset_destroy (ranks);
    free (s);
    free (p);
    return job;
}

/* Support adding a housekeeping object to the the 'job-manager.stats-get'
 * response in job-manager.c.
 */
json_t *housekeeping_get_stats (struct housekeeping *hk)
{
    json_t *running;
    json_t *stats = NULL;
    struct allocation *a;
    char *command = NULL;

    if (!(running = json_object ()))
        goto nomem;
    a = zlistx_first (hk->allocations);
    while (a) {
        json_t *job;
        if (!(job = housekeeping_get_stats_job (a))
            || json_object_set_new (running, idf58 (a->id), job) < 0) {
            json_decref (job);
            goto nomem;
        }
        a = zlistx_next (hk->allocations);
    }
    if (hk->cmd)
        command = flux_cmd_stringify (hk->cmd);
    if (!(stats = json_pack ("{s:O, s{s:f s:s}}",
                             "running", running,
                             "config",
                               "release-after", hk->release_after,
                               "command", command ? command : "")))
        goto nomem;
    free (command);
    json_decref (running);
    return stats;
nomem:
    free (command);
    json_decref (running);
    errno = ENOMEM;
    return NULL;
}

/* Support accounting for resources stuck in housekeeping when preparing the
 * 'job-manager.resource-status' response in alloc.c.
 */
int housekeeping_stat_append (struct housekeeping *hk,
                              struct rlist *rl,
                              flux_error_t *error)
{
    struct allocation *a;
    a = zlistx_first (hk->allocations);
    while (a) {
        if (rlist_append (rl, a->rl) < 0) {
            errprintf (error,
                       "%s: duplicate housekeeping allocation",
                       idf58 (a->id));
            return -1;
        }
        a = zlistx_next (hk->allocations);
    }
    return 0;
}

static void housekeeping_kill_cb (flux_t *h,
                                  flux_msg_handler_t *mh,
                                  const flux_msg_t *msg,
                                  void *arg)
{
    struct housekeeping *hk = arg;
    int signum;
    flux_jobid_t jobid = FLUX_JOBID_ANY;
    const char *ranks = NULL;
    struct idset *ids = NULL;
    idset_error_t error;
    const char *errmsg = NULL;
    struct allocation *a;
    flux_future_t *f;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:i s?I s?s}",
                             "signum", &signum,
                             "jobid", &jobid,
                             "ranks", &ranks) < 0)
        goto error;
    if (ranks) {
        if (!(ids = idset_decode_ex (ranks, -1, -1, 0, &error))) {
            errmsg = error.text;
            goto error;
        }
    }
    a = zlistx_first (hk->allocations);
    while (a) {
        if (a->id == jobid || jobid == FLUX_JOBID_ANY) {
            if (a->bulk_exec) {
                f = bulk_exec_kill (a->bulk_exec, ids, signum);
                if (flux_future_then (f, -1, kill_continuation, hk) < 0)
                    flux_future_destroy (f);
            }
        }
        a = zlistx_next (hk->allocations);
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to housekeeping-kill");
    idset_destroy (ids);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to housekeeping-kill");
    idset_destroy (ids);
}

static flux_cmd_t *create_cmd (json_t *cmdline)
{
    size_t index;
    json_t *value;
    char *argz = NULL;
    size_t argz_len = 0;
    int argc;
    char **argv = NULL;
    flux_cmd_t *cmd = NULL;

    json_array_foreach (cmdline, index, value) {
        if (!json_is_string (value)
            || argz_add (&argz, &argz_len, json_string_value (value)) != 0)
            goto done;
    }
    if ((argc = argz_count (argz, argz_len)) == 0
        || !(argv = calloc (argc + 1, sizeof (argv[0]))))
        goto done;
    argz_extract (argz, argz_len, argv);
    if (!(cmd = flux_cmd_create (argc, argv, environ)))
        goto done;
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
    json_t *housekeeping = NULL;
    flux_error_t e;
    json_error_t jerror;
    json_t *cmdline = NULL;
    const char *release_after_fsd = NULL;
    double release_after = default_release_after;
    flux_cmd_t *cmd = NULL;
    const char *imp_path = NULL;
    char *imp_path_cpy = NULL;
    int use_systemd_unit = 0;

    if (flux_conf_unpack (conf,
                          &e,
                          "{s?{s?o}}",
                          "job-manager",
                            "housekeeping", &housekeeping) < 0)
        return errprintf (error, "job-manager.housekeeping: %s", e.text);

    // if the housekeeping table is not present, housekeeping is not configured
    if (!housekeeping)
        goto done;

    if (json_unpack_ex (housekeeping,
                        &jerror,
                        0,
                        "{s?o s?s s?b !}",
                        "command", &cmdline,
                        "release-after", &release_after_fsd,
                        "use-systemd-unit", &use_systemd_unit) < 0)
        return errprintf (error, "job-manager.housekeeping: %s", jerror.text);

    if (use_systemd_unit) {
        flux_log (hk->ctx->h,
                  LOG_ERR,
                  "job-manager.housekeeping.use-systemd-unit is deprecated"
                  " - ignoring");
    }

    // let job-exec handle exec errors
    (void)flux_conf_unpack (conf, NULL, "{s?{s?s}}", "exec", "imp", &imp_path);

    if (release_after_fsd) {
        if (fsd_parse_duration (release_after_fsd, &release_after) < 0)
            return errprintf (error,
                              "job-manager.housekeeping.release-after"
                              " FSD parse error");
    }

    if (cmdline) {
        if (!(cmd = create_cmd (cmdline)))
            return errprintf (error, "error creating housekeeping command");
    }

    // if no command line was defined, assume "imp run housekeeping"
    else {
        if (!imp_path) {
            return errprintf (error,
                              "job-manager.housekeeping implies IMP"
                              " but exec.imp is undefined");
        }
        json_t *o;
        if ((o = json_pack ("[sss]", imp_path, "run", "housekeeping")))
            cmd = create_cmd (o);
        json_decref (o);
        if (!cmd)
            return errprintf (error, "error creating housekeeping command");
        if (!(imp_path_cpy = strdup (imp_path))) {
            flux_cmd_destroy (cmd);
            return errprintf (error, "error duplicating IMP path");
        }
    }
done:
    flux_cmd_destroy (hk->cmd);
    hk->cmd = cmd;
    free (hk->imp_path);
    hk->imp_path = imp_path_cpy;
    hk->release_after = release_after;
    flux_log (hk->ctx->h,
              LOG_DEBUG,
              "housekeeping is %sconfigured%s",
              hk->cmd ? "" : "not ",
              hk->imp_path ? " with IMP" : "");
    return 1; // allow dynamic changes
}

static const struct flux_msg_handler_spec htab[] = {
    {
        .typemask = FLUX_MSGTYPE_REQUEST,
        .topic_glob = "job-manager.housekeeping-kill",
        .cb = housekeeping_kill_cb,
        .rolemask = 0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

void housekeeping_ctx_destroy (struct housekeeping *hk)
{
    if (hk) {
        int saved_errno = errno;
        conf_unregister_callback (hk->ctx->conf, housekeeping_parse_config);
        flux_cmd_destroy (hk->cmd);
        zlistx_destroy (&hk->allocations);
        flux_msg_handler_delvec (hk->handlers);
        free (hk->imp_path);
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
    if (flux_msg_handler_addvec (ctx->h, htab, hk, &hk->handlers) < 0)
        goto error;
    return hk;
error:
    housekeeping_ctx_destroy (hk);
    return NULL;
}

static struct bulk_exec_ops bulk_ops = {
    .on_start = bulk_start,
    .on_exit = bulk_exit,
    .on_complete = bulk_complete,
    .on_output = bulk_output,
    .on_error = bulk_error,
};

// vi:ts=4 sw=4 expandtab
