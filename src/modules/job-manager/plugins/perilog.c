/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/*  perilog.c : execute a job manager prolog/epilog for jobs
 *
 *  Run prolog and/or epilog commands on rank 0 before jobs
 *   have been allocated or freed resources.
 *
 *  Notes:
 *
 *  - The job manager prolog is started at the RUN state.
 *
 *  - If a job gets a fatal exception while the prolog is
 *    running, the prolog is canceled and a SIGTERM signal
 *    is sent. After a configurable timeout, ranks on which
 *    the prolog is still active are drained.
 *
 *  - The epilog is started as a result of a "finish" event or
 *    when the prolog completes if a fatal job exception has been
 *    raised. Therefore the job manager epilog is always run if
 *    a prolog has run.
 *
 *  - Requires that a prolog and/or epilog command be configured
 *    in the [job-manager.prolog] and [job-manager.epilog]
 *    tables, e.g.
 *
 *     [job-manager.prolog]
 *     command = [ "command", "arg1", "arg2" ]
 *     timeout = "30m"
 *
 *  - The queue should be idle before unloading/reloading this
 *     plugin. Otherwise jobs may become stuck because a prolog
 *     or epilog in progress will result in a missing -finish
 *     event in the job's eventlog.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <regex.h>
#include <math.h>
#include "src/common/libmissing/macros.h"
#define EXIT_CODE(x) __W_EXITCODE(x,0)

#include <jansson.h>
#include <flux/core.h>
#include <flux/jobtap.h>
#include <flux/idset.h>

#include "src/common/libjob/job_hash.h"
#include "src/common/libjob/idf58.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/fsd.h"
#include "ccan/str/str.h"
#include "src/broker/state_machine.h" // for STATE_CLEANUP
#include "src/common/libsubprocess/bulk-exec.h"
#include "src/common/librlist/rlist.h"

extern char **environ;

/*
 *  Configuration for a single perilog process
 */
struct perilog_procdesc {
    flux_cmd_t *cmd;
    bool uses_imp;
    bool prolog;
    bool per_rank;
    bool cancel_on_exception;
    double timeout;
    double kill_timeout;
};

/*  Global prolog/epilog configuration
 */
static struct perilog_conf {
    bool initialized;

    char *imp_path;
    struct perilog_procdesc *prolog;
    struct perilog_procdesc *epilog;

    zhashx_t *processes;       /*  List of outstanding perilog_proc objects */
    zlistx_t *log_ignore;      /*  List of regex patterns to ignore in logs */
    flux_future_t *watch_f;    /*  Watch for broker entering CLEANUP state */
    bool shutting_down;        /*  True when broker has entered CLEANUP */
} perilog_config;


/*  Data for a prolog/epilog process
 */
struct perilog_proc {
    flux_plugin_t *p;
    flux_jobid_t id;
    uint32_t userid;
    json_t *R;
    bool prolog;
    bool cancel_on_exception;
    bool canceled;
    bool timedout;
    double kill_timeout;
    flux_future_t *kill_f;
    flux_future_t *drain_f;
    flux_watcher_t *timer;
    flux_watcher_t *kill_timer;
    struct bulk_exec *bulk_exec;
    struct idset *ranks;
    char *failed_ranks;
};

static struct perilog_proc *procdesc_run (flux_t *h,
                                          flux_plugin_t *p,
                                          struct perilog_procdesc *pd,
                                          flux_jobid_t id,
                                          uint32_t userid,
                                          json_t *R);

static void timeout_cb (flux_reactor_t *r,
                        flux_watcher_t *w,
                        int revents,
                        void *arg);

static void perilog_procdesc_destroy (struct perilog_procdesc *pd)
{
    if (pd) {
        int saved_errno = errno;
        flux_cmd_destroy (pd->cmd);
        free (pd);
        errno = saved_errno;
    }
}

static flux_cmd_t *cmd_from_json (json_t *o)
{
    size_t index;
    json_t *value;
    flux_cmd_t *cmd;

    if (!json_is_array (o))
        return NULL;

    if (!(cmd = flux_cmd_create (0, NULL, environ)))
        return NULL;

    json_array_foreach (o, index, value) {
        const char *arg = json_string_value (value);
        if (!value
            || flux_cmd_argv_append (cmd, arg) < 0)
            goto fail;
    }
    return cmd;
fail:
    flux_cmd_destroy (cmd);
    return NULL;
}


static struct perilog_procdesc *perilog_procdesc_create (json_t *o,
                                                         bool prolog,
                                                         flux_error_t *errp)
{
    struct perilog_procdesc *pd = NULL;
    int per_rank = 0;
    int cancel_on_exception = -1;
    const char *timeout;
    double kill_timeout = -1.;
    flux_cmd_t *cmd = NULL;
    json_t *command = NULL;
    bool uses_imp = false;
    json_error_t error;

    const char *name = prolog ? "prolog" : "epilog";

    /*  Set default timeout for prolog to 30m, unlimited for epilog
     */
    timeout = prolog ? "30m" : "0";

    if (json_unpack_ex (o,
                        &error,
                        0,
                        "{s?o s?s s?F s?b s?b !}",
                        "command", &command,
                        "timeout", &timeout,
                        "kill-timeout", &kill_timeout,
                        "per-rank", &per_rank,
                        "cancel-on-exception", &cancel_on_exception) < 0) {
        errprintf (errp, "%s", error.text);
        return NULL;
    }
    if (command && !json_is_array (command)) {
        errprintf (errp, "command must be an array");
        return NULL;
    }
    if (kill_timeout > 0.) {
        if (!prolog) {
            errprintf (errp, "kill-timeout not allowed for epilog");
            return NULL;
        }
    }
    /*  If no command is set but exec.imp is non-NULL then set command to
     *  [ "$imp_path", "run", "$name" ]
     */
    if (!command && perilog_config.imp_path) {
        json_t *imp_cmd;
        if ((imp_cmd = json_pack ("[sss]",
                                  perilog_config.imp_path,
                                  "run",
                                  name)))
            cmd = cmd_from_json (imp_cmd);
        json_decref (imp_cmd);
        if (!cmd) {
            errprintf (errp, "error creating %s command", name);
            return NULL;
        }
        uses_imp = true;
    }
    if (!(pd = calloc (1, sizeof (*pd)))) {
        errprintf (errp, "Out of memory");
        return NULL;
    }
    if (command && !(cmd = cmd_from_json (command))) {
        errprintf (errp, "malformed %s command", prolog ? "prolog" : "epilog");
        goto error;
    }
    if (timeout && fsd_parse_duration (timeout, &pd->timeout) < 0) {
        errprintf (errp, "invalid %s timeout", prolog ? "prolog" : "epilog");
        goto error;
    }
    /* Special case: INFINITY disables timeout so set timeout = 0.0:
     */
    if (pd->timeout == INFINITY)
        pd->timeout = 0.;
    if (!cmd) {
        errprintf (errp, "no command specified and exec.imp not defined");
        goto error;
    }

    pd->cmd = cmd;
    pd->kill_timeout = kill_timeout > 0. ? kill_timeout : 5.;
    pd->per_rank = per_rank;
    pd->prolog = prolog;
    pd->uses_imp = uses_imp;

    /* If cancel_on_exception unset, default to prolog=true, epilog=false
     * Otherwise, use set value:
     */
    if (cancel_on_exception < 0)
        pd->cancel_on_exception = prolog;
    else
        pd->cancel_on_exception = cancel_on_exception;

    return pd;
error:
    flux_cmd_destroy (cmd);
    perilog_procdesc_destroy (pd);
    return NULL;
}

static struct perilog_proc * perilog_proc_create (flux_plugin_t *p,
                                                  flux_jobid_t id,
                                                  uint32_t userid,
                                                  bool prolog)
{
    struct perilog_proc *proc = calloc (1, sizeof (*proc));
    if (proc == NULL)
        return NULL;
    proc->p = p;
    proc->id = id;
    proc->userid = userid;
    proc->prolog = prolog;
    if (zhashx_insert (perilog_config.processes, &proc->id, proc) < 0) {
        free (proc);
        errno = EEXIST;
        return NULL;
    }
    return proc;
}

static void perilog_proc_destroy (struct perilog_proc *proc)
{
    if (proc) {
        int saved_errno = errno;
        idset_destroy (proc->ranks);
        free (proc->failed_ranks);
        json_decref (proc->R);
        bulk_exec_destroy (proc->bulk_exec);
        flux_future_destroy (proc->kill_f);
        flux_future_destroy (proc->drain_f);
        flux_watcher_destroy (proc->timer);
        flux_watcher_destroy (proc->kill_timer);
        free (proc);
        errno = saved_errno;
    }
}

static const char *perilog_proc_name (struct perilog_proc *proc)
{
    return proc->prolog ? "prolog" : "epilog";
}

static double perilog_proc_timeout (struct perilog_proc *proc)
{
    if (proc->prolog)
        return perilog_config.prolog->timeout;
    return perilog_config.epilog->timeout;
}

/*  zhashx_destructor_fn prototype
 */
static void perilog_proc_destructor (void **item)
{
    if (item) {
        struct perilog_proc *proc = *item;
        /*  Delete this perilog_proc entry from job hash first,
         *  since job-exception handler detects if a perilog is currently
         *  executing by checking for the perilog_proc aux_item:
         */
        flux_jobtap_job_aux_set (proc->p,
                                 proc->id,
                                 "perilog_proc",
                                 NULL,
                                 NULL);
        perilog_proc_destroy (proc);
        *item = NULL;
    }
}

/*  delete process from global hash - calls perilog_proc_destructor()
 */
static void perilog_proc_delete (struct perilog_proc *proc)
{
    if (proc) {
        zhashx_delete (perilog_config.processes, &proc->id);
    }
}

static void emit_finish_event (struct perilog_proc *proc,
                               struct bulk_exec *bulk_exec)
{
    int status = bulk_exec_rc (bulk_exec);
    if (proc->prolog) {
        int rc;

        /*
         *  If prolog failed, raise job exception before prolog-finish
         *   event is emitted to ensure job isn't halfway started before
         *   the exception is raised:
         */
        if (status != 0 && !proc->canceled) {
            flux_t *h = flux_jobtap_get_flux (proc->p);
            int code = WIFEXITED (status) ? WEXITSTATUS (status) : -1;
            int sig;
            char *errmsg;
            char *hosts = NULL;

            if (!(hosts = flux_hostmap_lookup (h, proc->failed_ranks, NULL)))
                hosts = strdup ("unknown");

            if (proc->timedout) {
                rc = asprintf (&errmsg,
                               "prolog timed out on %s (rank %s)",
                               hosts,
                               proc->failed_ranks);
            }
            /*  Report that prolog was signaled if WIFSIGNALED() is true, or
             *  exit code > 128 (where standard exit code is 127+signo from
             *  most shells)
             */
            else if (WIFSIGNALED (status) || code > 128) {
                sig = WIFSIGNALED (status) ? WTERMSIG (status) : code - 128;
                rc = asprintf (&errmsg,
                               "prolog killed by signal %d on %s (rank %s)",
                               sig,
                               hosts ? hosts : "unknown",
                               proc->failed_ranks);
            }
            else
                rc = asprintf (&errmsg,
                               "prolog exited with code=%d on %s (rank %s)",
                               code,
                               hosts ? hosts : "unknown",
                               proc->failed_ranks);

            free (hosts);
            if (rc < 0)
                errmsg = NULL;
            if (flux_jobtap_raise_exception (proc->p,
                                             proc->id,
                                             "prolog",
                                             0,
                                             "%s",
                                             errmsg ?
                                             errmsg :
                                             "job prolog failed") < 0)
                    flux_log_error (flux_jobtap_get_flux (proc->p),
                                    "prolog-finish: jobtap_raise_exception");
            free (errmsg);
        }
        if (flux_jobtap_prolog_finish (proc->p,
                                       proc->id,
                                       "job-manager.prolog",
                                       status) < 0)
            flux_log_error (flux_jobtap_get_flux (proc->p),
                            "flux_jobtap_prolog_finish: id=%s status=%d",
                            idf58 (proc->id),
                            status);
    }
    else {
        /*
         *  Epilog complete: unsubscribe this plugin from the
         *   finished job and post an epilog-finish event.
         *
         *  No job exception is raised since the job is already exiting,
         *   and it is expected that the actual epilog script will
         *   drain nodes or take other action on failure if necessary.
         */
        flux_jobtap_job_unsubscribe (proc->p, proc->id);
        if (flux_jobtap_epilog_finish (proc->p,
                                       proc->id,
                                       "job-manager.epilog",
                                       status) < 0)
            flux_log_error (flux_jobtap_get_flux (proc->p),
                            "flux_jobtap_epilog_finish");
    }
}

static bool subprocess_failed (flux_subprocess_t *p)
{
    if (flux_subprocess_state (p) == FLUX_SUBPROCESS_FAILED
        || flux_subprocess_status (p) != 0)
        return true;
    return false;
}

/* Drain ranks that failed, are still active or both. */
static flux_future_t *proc_drain_ranks (struct perilog_proc *proc,
                                        bool drain_failed,
                                        bool drain_active)
{
    struct idset *failed = NULL;
    flux_future_t *f = NULL;
    unsigned long rank;
    const char *msg;
    char reason[256];
    flux_t *h = flux_jobtap_get_flux (proc->p);

    if (!(failed = idset_create (0, IDSET_FLAG_AUTOGROW))) {
        flux_log_error (h, "drain_failed_ranks: idset_create");
        goto out;
    }

    rank = idset_first (proc->ranks);
    while (rank != IDSET_INVALID_ID) {
        flux_subprocess_t *p;
        if ((p = bulk_exec_get_subprocess (proc->bulk_exec, rank))) {
            if ((drain_failed && subprocess_failed (p))
                || (drain_active && flux_subprocess_active (p))) {
                if (idset_set (failed, rank) < 0){
                    flux_log_error (h,
                                    "failed to add rank=%lu to drain set",
                                    rank);
                }
            }
        }
        rank = idset_next (proc->ranks, rank);
    }
    if (!(proc->failed_ranks = idset_encode (failed, IDSET_FLAG_RANGE))) {
        flux_log_error (h,
                        "%s: error encoding %s failed ranks",
                        idf58 (proc->id),
                        perilog_proc_name (proc));
        goto out;
    }

    if (proc->canceled)
        msg = "canceled then timed out";
    else if (proc->timedout)
        msg = "timed out";
    else
        msg = "failed";

    (void) snprintf (reason,
                     sizeof (reason),
                     "%s %s for job %s",
                     perilog_proc_name (proc),
                     msg,
                     idf58 (proc->id));

    if (!(f = flux_rpc_pack (h,
                             "resource.drain",
                             0,
                             0,
                             "{s:s s:s s:s}",
                             "targets", proc->failed_ranks,
                             "reason", reason,
                             "mode", "update"))) {
        flux_log (h,
                  LOG_ERR,
                  "%s: %s: failed to send drain RPC for ranks %s",
                  idf58 (proc->id),
                  perilog_proc_name (proc),
                  proc->failed_ranks);
        goto out;
    }
out:
    idset_destroy (failed);
    return f;
}

static bool perilog_proc_failed (struct perilog_proc *proc)
{
    if (proc->canceled
        || proc->timedout
        || bulk_exec_rc (proc->bulk_exec) > 0)
        return true;
    return false;
}

static void perilog_proc_finish (struct perilog_proc *proc)
{
    flux_t *h = flux_jobtap_get_flux (proc->p);
    flux_plugin_t *p;
    uint32_t userid;
    flux_jobid_t id;
    json_t *R;
    struct perilog_procdesc *pd;
    bool run_epilog = false;


    /*  If a prolog was completing, and it failed in some way, then there
     *  will be no finish event to trigger the epilog. However, an epilog
     *  should still be run in case it is required to clean up or revert
     *  something done by the prolog. So do that here.
     */
    if (proc->prolog
        && perilog_proc_failed (proc)
        && (pd = perilog_config.epilog)) {
        /* epilog process can't be started until prolog perilog_proc is
         * deleted, so capture necessary info here and set a boolean to
         * create the epilog before leaving this function.
         */
        run_epilog = true;
        p = proc->p;
        id = proc->id;
        userid = proc->userid;
        R = proc->R;

        /* The epilog-start event must be posted before the prolog-finish
         * event to avoid the job potentially going straight to INACTIVE
         * after the prolog-finish event is posted below
         */
        if (flux_jobtap_event_post_pack (p,
                                         id,
                                         "epilog-start",
                                         "{s:s}",
                                         "description",
                                         "job-manager.epilog") < 0) {
            flux_log_error (h,
                            "%s: failed to post epilog-start on prolog-finish",
                            idf58 (proc->id));
            run_epilog = false;
        }
    }
    emit_finish_event (proc, proc->bulk_exec);
    perilog_proc_delete (proc);

    if (run_epilog) {
        struct perilog_proc *epilog;

        if (!(epilog = procdesc_run (h, p, pd, id, userid, R))
            || flux_jobtap_job_aux_set (p,
                                        id,
                                        "perilog_proc",
                                        epilog,
                                        NULL) < 0) {
            flux_log_error (h,
                            "%s: failed to start epilog on prolog-finish",
                            idf58 (proc->id));

            /* Since epilog-start event was emitted above, we must emit an
             * epilog-finish event to avoid hanging the job
             */
            if (flux_jobtap_epilog_finish (p, id,"job-manager.epilog", 1) < 0) {
                flux_log_error (h,
                                "%s: failed to post epilog-finish event",
                                idf58 (proc->id));
            }
            perilog_proc_delete (epilog);
        }
    }
}

static void drain_failed_cb (flux_future_t *f, void *arg)
{
    struct perilog_proc *proc = arg;
    flux_t *h = flux_jobtap_get_flux (proc->p);

    if (flux_future_get (f, NULL) < 0) {
        flux_log (h,
                  LOG_ERR,
                  "Failed to drain ranks with failed %s for %s: %s",
                  perilog_proc_name (proc),
                  idf58 (proc->id),
                  future_strerror (f, errno));
    }
    /* future destroyed by perilog_proc_delete()
     */
    perilog_proc_finish (proc);
}

static bool perilog_per_rank (struct perilog_proc *proc)
{
    if (proc->prolog)
        return perilog_config.prolog->per_rank;
    return perilog_config.epilog->per_rank;
}

static void proc_drain_and_finish (struct perilog_proc *proc,
                                   bool drain_failed,
                                   bool drain_active)
{

    if (drain_failed || drain_active) {
        /* Drain the set of ranks that failed the prolog/epilog. If the
         * drain RPC is successful, then wait for the response before
         * emitting the "prolog/epilog-finish" event. O/w, resources could
         * be freed and handed out to new jobs before they are drained.
         */
        if ((proc->drain_f = proc_drain_ranks (proc,
                                               drain_failed,
                                               drain_active))
            && flux_future_then (proc->drain_f,
                                 -1.,
                                 drain_failed_cb,
                                 proc) == 0)
            return;

        /* O/w, drain RPC failed, fall through so finish event is still
         * emitted.
         */
    }
    perilog_proc_finish (proc);
}

static void completion_cb (struct bulk_exec *bulk_exec, void *arg)
{
    struct perilog_proc *proc = bulk_exec_aux_get (bulk_exec, "perilog_proc");
    if (proc) {
        bool drain_failed = false;

        if (perilog_per_rank (proc)
            && !proc->canceled
            && bulk_exec_rc (bulk_exec) != 0)
            drain_failed = true;

        proc_drain_and_finish (proc, drain_failed, false);
    }
}

static void error_cb (struct bulk_exec *bulk_exec,
                      flux_subprocess_t *p,
                      void *arg)
{
    struct perilog_proc *proc = bulk_exec_aux_get (bulk_exec, "perilog_proc");
    flux_t *h = flux_jobtap_get_flux (proc->p);
    int rank = flux_subprocess_rank (p);
    const char *hostname = flux_get_hostbyrank (h, rank);
    const char *error = flux_subprocess_fail_error (p);

    if (!proc)
        return;

    flux_log (h,
              LOG_ERR,
              "%s: %s: %s (rank %d): %s",
              idf58 (proc->id),
              perilog_proc_name (proc),
              hostname,
              rank,
              error);
}

static bool perilog_log_ignore (struct perilog_conf *conf, const char *s)
{
    if (conf->log_ignore) {
        const regex_t *reg = zlistx_first (conf->log_ignore);
        while (reg) {
            if (regexec (reg, s, 0, NULL, 0) == 0)
                return true;
            reg = zlistx_next (conf->log_ignore);
        }
    }
    return false;
}

static void io_cb (struct bulk_exec *bulk_exec,
                   flux_subprocess_t *sp,
                   const char *stream,
                   const char *data,
                   int len,
                   void *arg)
{
    flux_t *h;
    struct perilog_proc *proc;
    char buf[len+1]; /* bulk_exec output is not NUL terminated */

    if (len <= 0
        || !(proc = bulk_exec_aux_get (bulk_exec, "perilog_proc"))
        || !(h = flux_jobtap_get_flux (proc->p)))
        return;

    /* Copy data to NUL terminated buffer */
    memcpy (buf, data, len);
    buf [len] = '\0';

    if (!perilog_log_ignore (&perilog_config, buf)) {
        int level = LOG_INFO;
        int rank = flux_subprocess_rank (sp);
        const char *hostname = flux_get_hostbyrank (h, rank);

        if (streq (stream, "stderr"))
            level = LOG_ERR;
        flux_log (h,
                  level,
                  "%s: %s: %s (rank %d): %s: %s",
                  idf58 (proc->id),
                  perilog_proc_name (proc),
                  hostname,
                  rank,
                  stream,
                  buf);
    }
}

static struct bulk_exec_ops ops = {
        .on_start = NULL,
        .on_exit = NULL,
        .on_complete = completion_cb,
        .on_error = error_cb,
        .on_output = io_cb,
};

static struct idset *ranks_from_R (json_t *R)
{
    struct idset *ranks;
    struct rlist *rl = rlist_from_json (R, NULL);
    if (!rl)
        return NULL;
    ranks = rlist_ranks (rl);
    rlist_destroy (rl);
    return ranks;
}

static struct perilog_proc *procdesc_run (flux_t *h,
                                          flux_plugin_t *p,
                                          struct perilog_procdesc *pd,
                                          flux_jobid_t id,
                                          uint32_t userid,
                                          json_t *R)
{
    struct perilog_proc *proc = NULL;
    struct idset *ranks = NULL;
    struct bulk_exec *bulk_exec = NULL;
    double timeout;

    if (!(proc = perilog_proc_create (p, id, userid, pd->prolog))) {
        flux_log_error (h,
                        "%s: proc_create",
                        pd->prolog ? "prolog" : "epilog");
        goto error;
    }
    if (flux_cmd_setenvf (pd->cmd, 1, "FLUX_JOB_ID", "%s", idf58 (id)) < 0
        || flux_cmd_setenvf (pd->cmd, 1, "FLUX_JOB_USERID", "%u", userid) < 0) {
        flux_log_error (h,
                        "%s: flux_cmd_create",
                        perilog_proc_name (proc));
        goto error;
    }
    if (pd->per_rank) {
        if (!(ranks = ranks_from_R (R))) {
            flux_log (h,
                      LOG_ERR,
                      "%s: %s: failed to decode ranks from R",
                      idf58 (id),
                      perilog_proc_name (proc));
            goto error;
        }
    }
    else if (!(ranks = idset_decode ("0"))) {
        flux_log_error (h, "%s: idset_decode", perilog_proc_name (proc));
        goto error;
    }

    if (!(bulk_exec = bulk_exec_create (&ops,
                                        "rexec",
                                        id,
                                        perilog_proc_name (proc),
                                        NULL))
        || bulk_exec_push_cmd (bulk_exec, ranks, pd->cmd, 0) < 0) {
        flux_log_error (h,
                        "failed to create %s bulk exec cmd for %s",
                        perilog_proc_name (proc),
                        idf58 (id));
        goto error;
    }
    /*  If using IMP, push path to IMP into bulk_exec for IMP kill support:
     */
    if (pd->uses_imp
        && bulk_exec_set_imp_path (bulk_exec, perilog_config.imp_path) < 0) {
        flux_log_error (h,
                        "%s: failed to set IMP path",
                        perilog_proc_name (proc));
        goto error;
    }
    if (bulk_exec_start (h, bulk_exec) < 0) {
        flux_log_error (h, "%s: bulk_exec_start", perilog_proc_name (proc));
        goto error;
    }
    if (bulk_exec_aux_set (bulk_exec, "perilog_proc", proc, NULL) < 0) {
        flux_log_error (h,
                        "%s: bulk_exec_aux_set",
                        perilog_proc_name (proc));
        goto error;
    }
    timeout = perilog_proc_timeout (proc);
    if (timeout > 0.0) {
        flux_watcher_t *w;
        if (!(w = flux_timer_watcher_create (flux_get_reactor (h),
                                             timeout,
                                             0.,
                                             timeout_cb,
                                             proc))) {
            flux_log_error (h,
                            "%s: failed to create timeout timer",
                            perilog_proc_name (proc));
            goto error;
        }
        flux_watcher_start (w);
        proc->timer = w;
    }
    proc->R = json_incref (R);
    proc->bulk_exec = bulk_exec;
    proc->ranks = ranks;
    proc->cancel_on_exception = pd->cancel_on_exception;
    proc->kill_timeout = pd->kill_timeout;

    /* proc now has ownership of bulk_exec, ranks
     */
    return proc;
error:
    idset_destroy (ranks);
    bulk_exec_destroy (bulk_exec);
    perilog_proc_destroy (proc);
    return NULL;
}

static int run_command (flux_plugin_t *p,
                        flux_plugin_arg_t *args,
                        struct perilog_procdesc *pd)
{
    flux_t *h = flux_jobtap_get_flux (p);
    struct perilog_proc *proc = NULL;
    flux_jobid_t id;
    uint32_t userid;
    json_t *R;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I s:i s:o}",
                                "id", &id,
                                "userid", &userid,
                                "R", &R) < 0) {
        flux_log_error (h, "flux_plugin_arg_unpack");
        return -1;
    }

    if (!(proc = procdesc_run (h, p, pd, id, userid, R)))
        return -1;

    if (flux_jobtap_job_aux_set (p,
                                 FLUX_JOBTAP_CURRENT_JOB,
                                 "perilog_proc",
                                 proc,
                                 NULL) < 0) {
        flux_log_error (h,
                        "%s: flux_jobtap_job_aux_set",
                        perilog_proc_name (proc));
        goto error;
    }
    return 0;
error:
    perilog_proc_destroy (proc);
    return 01;
}

static int run_cb (flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *args,
                   void *arg)
{

    /*
     *  Subscribe to job events if an epilog or prolog command is
     *   registered. This is needed to allow this plugin to subscribe
     *   to the finish event for the epilog, and any exception events
     *   for the prolog (so it can be canceled).
     */
    if (perilog_config.epilog || perilog_config.prolog) {
        if (flux_jobtap_job_subscribe (p, FLUX_JOBTAP_CURRENT_JOB) < 0) {
            flux_jobtap_raise_exception (p,
                                         FLUX_JOBTAP_CURRENT_JOB,
                                         "prolog",
                                         0,
                                         "failed to subscribe to job events");
            return -1;
        }
    }

    if (perilog_config.prolog == NULL)
        return 0;

    if (run_command (p, args, perilog_config.prolog) < 0) {
        flux_jobtap_raise_exception (p,
                                     FLUX_JOBTAP_CURRENT_JOB,
                                     "prolog",
                                     0,
                                     "failed to start job prolog");
        return -1;
    }
    return flux_jobtap_prolog_start (p, "job-manager.prolog");
}

static int job_finish_cb (flux_plugin_t *p,
                          const char *topic,
                          flux_plugin_arg_t *args,
                          void *arg)
{
    if (perilog_config.epilog == NULL)
        return 0;

    /*  Don't start new epilog processes if the broker is shutting down.
     *   Flux currently cancels running jobs as part of shutdown.  If the
     *   broker takes longer than the systemd TimeoutStopSec (e.g. 90s) to
     *   stop, it may be killed and data may be lost.  Since epilog scripts
     *   are site-defined and may take an arbitrarily long time to run,
     *   simply skip them during shutdown.  This may be relaxed once Flux
     *   is capable of restarting with running jobs.
     */
    if (perilog_config.shutting_down)
        return 0;

    if (run_command (p, args, perilog_config.epilog) < 0) {
        flux_jobtap_raise_exception (p,
                                     FLUX_JOBTAP_CURRENT_JOB,
                                     "epilog",
                                     0,
                                     "failed to start job epilog");
        return -1;
    }
    return flux_jobtap_epilog_start (p, "job-manager.epilog");
}

static void proc_kill_cb (flux_future_t *f, void *arg)
{
    struct perilog_proc *proc = arg;
    flux_t *h = flux_future_get_flux (f);

    if (flux_future_get (f, NULL) < 0) {
        flux_log_error (h,
                        "%s: Failed to signal job %s",
                        idf58 (proc->id),
                        perilog_proc_name (proc));
    }
}

static int proc_kill (struct perilog_proc *proc)
{
    flux_t *h = flux_jobtap_get_flux (proc->p);

    if (proc->kill_timer)
        return 0;

    if (!(proc->kill_f = bulk_exec_kill (proc->bulk_exec, NULL, SIGTERM)))
        return -1;

    if (flux_future_then (proc->kill_f, -1., proc_kill_cb, proc) < 0) {
        flux_log_error (h, "proc_kill: flux_future_then");
        flux_future_destroy (proc->kill_f);
        proc->kill_f = NULL;
        return -1;
    }

    return 0;
}

static void proc_kill_timeout_cb (flux_reactor_t *r,
                                  flux_watcher_t *w,
                                  int revents,
                                  void *arg)
{
    struct perilog_proc *proc = arg;
    flux_t *h = flux_jobtap_get_flux (proc->p);
    flux_log_error (h,
                    "%s: timed out waiting for SIGTERM to terminate %s",
                    idf58 (proc->id),
                    perilog_proc_name (proc));
    /*  Drain active ranks and post finish event
     */
    proc_drain_and_finish (proc, false, true);
}

static int proc_kill_timer_start (struct perilog_proc *proc, double timeout)
{
    if (proc->kill_timer == NULL) {
        flux_t *h = flux_jobtap_get_flux (proc->p);
        flux_reactor_t *r = flux_get_reactor (h);
        proc->kill_timer = flux_timer_watcher_create (r,
                                                      timeout,
                                                      0.,
                                                      proc_kill_timeout_cb,
                                                      proc);
        if (!proc->kill_timer) {
            flux_log_error (h,
                            "%s: failed to start %s kill timer",
                            idf58 (proc->id),
                            perilog_proc_name (proc));
            /* Since timer cb won't be run, drain and send finish event now
             */
            proc_drain_and_finish (proc, false, true);
            return -1;
        }
        flux_watcher_start (proc->kill_timer);
    }
    return 0;
}

static void timeout_cb (flux_reactor_t *r,
                        flux_watcher_t *w,
                        int revents,
                        void *arg)
{
    struct perilog_proc *proc = arg;
    proc->timedout = true;
    if (proc_kill (proc) < 0)
        flux_log_error (flux_jobtap_get_flux (proc->p),
                        "failed to kill %s for %s",
                        perilog_proc_name (proc),
                        idf58 (proc->id));
    (void) proc_kill_timer_start (proc, proc->kill_timeout);
}

static int exception_cb (flux_plugin_t *p,
                         const char *topic,
                         flux_plugin_arg_t *args,
                         void *arg)
{
    /*  On exception, kill any prolog running for this job:
     *   Follow up with SIGKILL after 10s.
     */
    struct perilog_proc *proc;
    int severity;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:{s:{s:i}}}",
                                "entry",
                                "context",
                                "severity", &severity) < 0)
        return -1;

    if (severity == 0
        && (proc = flux_jobtap_job_aux_get (p,
                                           FLUX_JOBTAP_CURRENT_JOB,
                                           "perilog_proc"))
        && proc->cancel_on_exception
        && !proc->canceled
        && bulk_exec_active_count (proc->bulk_exec) > 0) {

        /* Set canceled flag to disable draining of failed prolog nodes
         */
        proc->canceled = true;
        if (proc_kill (proc) < 0
            || proc_kill_timer_start (proc, proc->kill_timeout) < 0)
            return -1;
    }
    return 0;
}

static regex_t *regexp_create (const char *pattern)
{
    regex_t *reg = calloc (1, sizeof (*reg));
    if (!reg)
        return NULL;
    if (regcomp (reg, pattern, REG_EXTENDED | REG_NOSUB) != 0) {
        free (reg);
        return NULL;
    }
    return reg;
}

static void regexp_destroy (regex_t *reg)
{
    if (reg) {
        int saved_errno = errno;
        regfree (reg);
        free (reg);
        errno = saved_errno;
    }
}

static void regexp_free (void **item)
{
    if (item) {
        regex_t *reg = *item;
        regexp_destroy (reg);
        reg = NULL;
    }
}

static int regexp_list_append (zlistx_t *l,
                               const char *pattern,
                               flux_error_t *errp);

static zlistx_t *regexp_list_create ()
{
    zlistx_t *l = NULL;
    if (!(l = zlistx_new ()))
        return NULL;
    zlistx_set_destructor (l, regexp_free);

    /*  Always ignore empty lines
     */
    if (regexp_list_append (l, "^\\s*$", NULL) < 0) {
        zlistx_destroy (&l);
        return NULL;
    }
    return l;
}

static int regexp_list_append (zlistx_t *l,
                               const char *pattern,
                               flux_error_t *errp)
{
    regex_t *reg = NULL;
    if (!(reg = regexp_create (pattern))) {
        errprintf (errp, "Failed to compile regex: %s", pattern);
        return -1;
    }
    if (!zlistx_add_end (l, reg)) {
        regexp_destroy (reg);
        errprintf (errp, "Out of memory adding regex pattern");
        return -1;
    }
    return 0;
}

static int regexp_list_append_array (zlistx_t *l,
                                     json_t *array,
                                     flux_error_t *errp)
{
    size_t index;
    json_t *entry;

    if (!json_is_array (array)) {
        errprintf (errp, "not an array");
        return -1;
    }

    json_array_foreach (array, index, entry) {
        const char *pattern = json_string_value (entry);
        if (pattern == NULL) {
            errprintf (errp, "all entries must be a string value");
            return -1;
        }
        if (regexp_list_append (l, pattern, errp) < 0)
            return -1;
    }
    return 0;
}

static void monitor_continuation (flux_future_t *f, void *arg)
{
    struct perilog_conf *conf = arg;
    flux_t *h = flux_future_get_flux (f);
    int state = -1;

    if (flux_rpc_get_unpack (f, "{s:i}", "state", &state) < 0) {
        if (errno != ENODATA) {
            flux_log (h,
                      LOG_ERR,
                      "error watching broker state: %s",
                      future_strerror (f, errno));
        }
        return;
    }
    if (state == STATE_CLEANUP) // the broker state, not a job state!
        conf->shutting_down = true;
    flux_future_reset (f);
}

static void free_config (struct perilog_conf *conf)
{
    flux_future_destroy (conf->watch_f);
    perilog_procdesc_destroy (conf->prolog);
    perilog_procdesc_destroy (conf->epilog);
    zhashx_destroy (&conf->processes);
    zlistx_destroy (&conf->log_ignore);
}

/*   Initialize a perilog_config object
 */
static int conf_init (flux_plugin_t *p, struct perilog_conf *conf)
{
    flux_t *h = flux_jobtap_get_flux (p);

    memset (conf, 0, sizeof (*conf));
    conf->initialized = true;

    if (!(conf->processes = job_hash_create ())) {
        flux_log (h, LOG_ERR, "perilog: failed to create job hash");
        return -1;
    }
    zhashx_set_destructor (conf->processes,
                           perilog_proc_destructor);

    /* Watch for broker transition to CLEANUP.
     */
    if (!(conf->watch_f = flux_rpc_pack (h,
                                         "state-machine.monitor",
                                          0,
                                          FLUX_RPC_STREAMING,
                                          "{s:i}",
                                          "final", STATE_CLEANUP))
        || flux_future_then (conf->watch_f,
                             -1,
                             monitor_continuation,
                             conf) < 0) {
        flux_log_error (h, "perilog: error watching broker state");
        goto error;
    }

    /* Free config at plugin destruction:
     */
    if (flux_plugin_aux_set (p, NULL, conf, (flux_free_f) free_config) < 0)
        goto error;

    return 0;
error:
    free_config (conf);
    return -1;
}

static json_t *proc_to_json (struct perilog_proc *proc)
{
    const char *state;
    struct idset *active_ranks;
    char *ranks = NULL;
    json_t *o;
    int total = bulk_exec_total (proc->bulk_exec);
    int active = total - bulk_exec_complete (proc->bulk_exec);

    if (proc->canceled)
        state = "canceled";
    else if (proc->timedout)
        state = "timeout";
    else
        state = "running";

    if ((active_ranks = bulk_exec_active_ranks (proc->bulk_exec)))
        ranks = idset_encode (active_ranks, IDSET_FLAG_RANGE);

    o = json_pack ("{s:s s:s s:i s:i s:s}",
                   "name", perilog_proc_name (proc),
                   "state", state,
                   "total", total,
                   "active", active,
                   "active_ranks", ranks ? ranks : "");
    free (ranks);
    idset_destroy (active_ranks);
    return o;
}

static json_t *cmdline_tojson (flux_cmd_t *cmd)
{
    int argc;
    json_t *o = json_array ();

    if (!o)
        return NULL;

    argc = flux_cmd_argc (cmd);
    for (int i = 0; i < argc; i++) {
        json_t *arg;
        if ((!(arg = json_string (flux_cmd_arg (cmd, i))))
            || json_array_append_new (o, arg) < 0) {
            json_decref (arg);
            goto error;
        }
    }
    return o;
error:
    json_decref (o);
    return NULL;
}

static json_t *procdesc_to_json (struct perilog_procdesc *pd)
{
    json_t *cmd = NULL;
    json_t *o;

    if (!pd || !pd->cmd)
        return json_object ();

    if (!(cmd = cmdline_tojson (pd->cmd)))
        return NULL;

    o = json_pack ("{s:O s:b s:b s:f s:f}",
                   "command", cmd,
                   "per_rank", pd->per_rank,
                   "cancel_on_exception", pd->cancel_on_exception,
                   "timeout", pd->timeout,
                   "kill-timeout", pd->kill_timeout);

    json_decref (cmd);
    return o;
}

static json_t *conf_to_json (struct perilog_conf *conf)
{
    json_t *o = NULL;
    json_t *prolog = procdesc_to_json (perilog_config.prolog);
    json_t *epilog = procdesc_to_json (perilog_config.epilog);


    if (!prolog || !epilog)
        goto out;
    o = json_pack ("{s:O s:O}",
                   "prolog", prolog,
                   "epilog", epilog);
out:
    json_decref (prolog);
    json_decref (epilog);
    return o;
}

static json_t *procs_to_json (zhashx_t *processes)
{
    struct perilog_proc *proc;
    json_t *o = NULL;

    if (!(o = json_object ()))
        return NULL;

    proc = zhashx_first (processes);
    while (proc) {
        json_t *entry;
        if (!(entry = proc_to_json (proc))
            || json_object_set_new (o, idf58 (proc->id), entry) < 0) {
            json_decref (entry);
            goto error;
        }
        proc = zhashx_next (processes);
    }
    return o;
error:
    json_decref (o);
    return NULL;
}

static int query_cb (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *args,
                     void *arg)
{
    flux_t *h = flux_jobtap_get_flux (p);
    json_t *conf = NULL;
    json_t *procs = NULL;
    int rc = -1;

    if (!(conf = conf_to_json (&perilog_config))
        || !(procs = procs_to_json (perilog_config.processes))) {
        flux_log (h,
                  LOG_ERR,
                  "perilog: failed to create query_cb json results");
        goto out;
    }

    if ((rc = flux_plugin_arg_pack (args,
                                    FLUX_PLUGIN_ARG_OUT,
                                    "{s:O s:O}",
                                    "conf", conf,
                                    "procs", procs)) < 0)
        flux_log_error (h,
                        "perilog: query_cb: flux_plugin_arg_pack: %s",
                        flux_plugin_arg_strerror (args));
out:
    json_decref (conf);
    json_decref (procs);
    return rc;
}

static int conf_update_cb (flux_plugin_t *p,
                           const char *topic,
                           flux_plugin_arg_t *args,
                           void *arg)
{
    json_t *conf;
    flux_error_t error;
    json_error_t jerror;
    const char *imp_path = NULL;
    json_t *prolog_config = NULL;
    json_t *epilog_config = NULL;
    struct perilog_procdesc *prolog = NULL;
    struct perilog_procdesc *epilog = NULL;
    json_t *log_ignore_config = NULL;
    zlistx_t *log_ignore = NULL;

    /* Perform one-time initialization of config if necessary
     */
    if (!perilog_config.initialized
        && conf_init (p, &perilog_config) < 0) {
        errprintf (&error, "failed to initialize perilog config");
        goto error;
    }

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:o}",
                                "conf", &conf) < 0) {
        errprintf (&error,
                   "perilog: error unpackage conf.update arguments: %s",
                   flux_plugin_arg_strerror (args));
        goto error;
    }

    if (json_unpack_ex (conf,
                        &jerror,
                        0,
                        "{s?{s?s} s?{s?o s?o s?{s?o}}}",
                        "exec",
                          "imp", &imp_path,
                        "job-manager",
                          "prolog", &prolog_config,
                          "epilog", &epilog_config,
                          "perilog",
                            "log-ignore", &log_ignore_config) < 0) {
        errprintf (&error,
                   "perilog: error unpacking config: %s",
                   jerror.text);
        goto error;
    }

    /*  Capture IMP path before first call to perilog_procdesc_create()
     */
    free (perilog_config.imp_path);
    perilog_config.imp_path = NULL;
    if (imp_path) {
        if (!(perilog_config.imp_path = strdup (imp_path))) {
            errprintf (&error, "failed to duplicate imp_path");
            goto error;
        }
    }

    if (prolog_config) {
        flux_error_t perr;
        if (!(prolog = perilog_procdesc_create (prolog_config,
                                                true,
                                                &perr))) {
            errprintf (&error,
                       "[job-manager.prolog]: %s",
                       perr.text);
            goto error;
        }
    }
    if (epilog_config) {
        flux_error_t perr;
        if (!(epilog = perilog_procdesc_create (epilog_config,
                                                false,
                                                &perr))) {
            errprintf (&error,
                       "[job-manager.epilog]: %s",
                       perr.text);
            goto error;
        }
    }

    /* Always start with default log_ignore list (ignores empty lines)
     */
    if (!(log_ignore = regexp_list_create ())) {
        errprintf (&error, "Out of memory creating log_ignore list");
        goto error;
    }
    if (log_ignore_config) {
        flux_error_t perr;
        if (regexp_list_append_array (log_ignore,
                                      log_ignore_config,
                                      &perr) < 0) {
            errprintf (&error,
                       "[job-manager.perilog]: error parsing log-ignore: %s",
                       perr.text);
            goto error;
        }
    }

    /*  Swap config:
     */
    perilog_procdesc_destroy (perilog_config.prolog);
    perilog_config.prolog = prolog;

    perilog_procdesc_destroy (perilog_config.epilog);
    perilog_config.epilog = epilog;

    zlistx_destroy (&perilog_config.log_ignore);
    perilog_config.log_ignore = log_ignore;

    return 0;
error:
    perilog_procdesc_destroy (epilog);
    perilog_procdesc_destroy (prolog);
    zlistx_destroy (&log_ignore);
    return flux_jobtap_error (p, args, "%s", error.text);
}

static const struct flux_plugin_handler tab[] = {
    { "job.state.run",       run_cb,       NULL },
    { "job.event.finish",    job_finish_cb,NULL },
    { "job.event.exception", exception_cb, NULL },
    { "conf.update",  conf_update_cb, NULL },
    { "plugin.query", query_cb, NULL },
    { 0 }
};

int flux_plugin_init (flux_plugin_t *p)
{
    perilog_config.initialized = false;
    return flux_plugin_register (p, "perilog", tab);
}

// vi:ts=4 sw=4 expandtab
