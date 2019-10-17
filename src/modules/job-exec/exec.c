/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Flux subprocess-based exec implementation
 *
 * DESCRIPTION
 *
 * Launch configured job shell, one per rank.
 *
 * TEST CONFIGURATION
 *
 * Test and other configuration may be presented in the jobspec
 * attributes.system.exec.bulkexec object. Supported keys include
 *
 * {
 *    "mock_exception":s       - Generate a mock execption in phase:
 *                               "init", or "starting"
 * }
 *
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <unistd.h>

#include "job-exec.h"
#include "bulk-exec.h"
#include "rset.h"

extern char **environ;
static const char *default_cwd = "/tmp";
static const char *default_job_shell = NULL;

/* Configuration for "bulk" execution implementation. Used only for testing
 *  for now.
 */
struct exec_conf {
    const char *        mock_exception;   /* fake exception */
};

static void exec_conf_destroy (struct exec_conf *tc)
{
    free (tc);
}

static struct exec_conf *exec_conf_create (json_t *jobspec)
{
    struct exec_conf *conf = calloc (1, sizeof (*conf));
    if (conf == NULL)
        return NULL;
    (void) json_unpack (jobspec, "{s:{s:{s:{s:{s:s}}}}}",
                                 "attributes", "system", "exec",
                                     "bulkexec",
                                         "mock_exception",
                                         &conf->mock_exception);
    return conf;
}

static const char * exec_mock_exception (struct bulk_exec *exec)
{
    struct exec_conf *conf = bulk_exec_aux_get (exec, "conf");
    if (!conf || !conf->mock_exception)
        return "none";
    return conf->mock_exception;
}

static const char *jobspec_get_job_shell (json_t *jobspec)
{
    const char *path = NULL;
    (void) json_unpack (jobspec, "{s:{s:{s:{s:s}}}}",
                                 "attributes", "system", "exec",
                                 "job_shell", &path);
    return path;
}

static const char *default_job_shell_path (void)
{
    if (!default_job_shell)
        default_job_shell = flux_conf_builtin_get ("shell_path",
                                                   FLUX_CONF_AUTO);
    return default_job_shell;
}

static const char *job_shell_path (struct jobinfo *job)
{
    const char *path = jobspec_get_job_shell (job->jobspec);
    if (!path && !(path = flux_attr_get (job->h, "job-exec.job-shell")))
        path = default_job_shell_path ();
    return path;
}

static const char *jobspec_get_cwd (json_t *jobspec)
{
    const char *cwd = NULL;
    (void) json_unpack (jobspec, "{s:{s:{s:s}}}",
                                 "attributes", "system",
                                 "cwd", &cwd);
    return cwd;
}

static const char *job_get_cwd (struct jobinfo *job)
{
    const char *cwd = jobspec_get_cwd (job->jobspec);
    if (!cwd)
        cwd = default_cwd;
    return (cwd);
}

/* Call bulk_exec_push_cmd() with idset containing only 'rank'.
 */
int push_one_cmd (struct bulk_exec *exec,
                  unsigned int rank,
                  flux_cmd_t *cmd,
                  int flags)
{
    struct idset *idset;
    if (!(idset = idset_create (rank + 1, 0)))
        return -1;
    if (idset_set (idset, rank) < 0)
        goto err;
    if (bulk_exec_push_cmd (exec, idset, cmd, flags) < 0)
        goto err;
    idset_destroy (idset);
    return 0;
err:
    idset_destroy (idset);
    return -1;
}

/* Push command to launch flux-remote (ssh) on 'broker_rank',
   which in turn launches flux-shell on 'host' (as 'target_rank').
 */
static int push_foreign_cmd (struct bulk_exec *exec,
                             struct jobinfo *job,
                             unsigned int target_rank,
                             unsigned int broker_rank,
                             const char *host)
{
    flux_cmd_t *cmd;

    if (!(cmd = flux_cmd_create (0, NULL, environ)))
        return -1;
    if (flux_cmd_setenvf (cmd, 1, "FLUX_KVS_NAMESPACE", "%s", job->ns) < 0)
        goto err;
    if (flux_cmd_argv_append (cmd, "flux") < 0
        || flux_cmd_argv_append (cmd, "remote") < 0
        || flux_cmd_argv_append (cmd, host) < 0
        || flux_cmd_argv_append (cmd, job_shell_path (job)) < 0
        || flux_cmd_argv_appendf (cmd, "--target-rank=%u", target_rank) < 0
        || flux_cmd_argv_appendf (cmd, "%ju", (uintmax_t) job->id) < 0)
        goto err;
    if (flux_cmd_setcwd (cmd, job_get_cwd (job)) < 0)
        goto err;
    if (push_one_cmd (exec, broker_rank, cmd, 0) < 0)
        goto err;
    flux_cmd_destroy (cmd);
    return 0;
err:
    flux_cmd_destroy (cmd);
    return -1;
}

/* Push command to run flux-shell on 'ranks' idset.
 * N.B. Shell assumes target_rank == broker_rank if no --target-rank opt
 */
static int push_native_cmd (struct bulk_exec *exec,
                            struct jobinfo *job,
                            const struct idset *ranks)
{
    flux_cmd_t *cmd;

    if (!(cmd = flux_cmd_create (0, NULL, environ)))
        return -1;
    if (flux_cmd_setenvf (cmd, 1, "FLUX_KVS_NAMESPACE", "%s", job->ns) < 0)
        goto err;
    if (flux_cmd_argv_append (cmd, job_shell_path (job)) < 0
        || flux_cmd_argv_appendf (cmd, "%ju", (uintmax_t) job->id) < 0)
        goto err;
    if (flux_cmd_setcwd (cmd, job_get_cwd (job)) < 0)
        goto err;
    if (bulk_exec_push_cmd (exec, ranks, cmd, 0) < 0)
        goto err;
    flux_cmd_destroy (cmd);
    return 0;
err:
    flux_cmd_destroy (cmd);
    return -1;
}

static void start_cb (struct bulk_exec *exec, void *arg)
{
    struct jobinfo *job = arg;
    jobinfo_started (job, NULL);
}

static void complete_cb (struct bulk_exec *exec, void *arg)
{
    struct jobinfo *job = arg;
    jobinfo_tasks_complete (job,
                            resource_set_ranks (job->R),
                            bulk_exec_rc (exec));
}

static void output_cb (struct bulk_exec *exec, flux_subprocess_t *p,
                       const char *stream,
                       const char *data,
                       int data_len,
                       void *arg)
{
    struct jobinfo *job = arg;
    flux_log (job->h, LOG_INFO, "%ju: %d: %s: %s",
                      (uintmax_t) job->id,
                      flux_subprocess_rank (p),
                      stream, data);
}

static void error_cb (struct bulk_exec *exec, flux_subprocess_t *p, void *arg)
{
    struct jobinfo *job = arg;
    const char *arg0 = flux_cmd_arg (flux_subprocess_get_cmd (p), 0);
    jobinfo_fatal_error (job, flux_subprocess_fail_errno (p),
                              "cmd=%s: rank=%d failed",
                              arg0, flux_subprocess_rank (p));
}

static struct bulk_exec_ops exec_ops = {
    .on_start =     start_cb,
    .on_exit =      NULL,
    .on_complete =  complete_cb,
    .on_output =    output_cb,
    .on_error =     error_cb
};

static int exec_init (struct jobinfo *job)
{
    struct exec_conf *conf = NULL;
    struct bulk_exec *exec = NULL;
    const struct idset *targets = NULL;
    unsigned int target_rank;
    struct idset *native_targets = NULL;

    if (!(targets = resource_set_ranks (job->R))) {
        flux_log_error (job->h, "exec_init: resource_set_ranks");
        goto err;
    }
    if (!(native_targets = idset_create (idset_last (targets) + 1, 0))) {
        flux_log_error (job->h, "exec_init: idset_create");
        goto err;
    }
    if (!(exec = bulk_exec_create (&exec_ops, job))) {
        flux_log_error (job->h, "exec_init: bulk_exec_create");
        goto err;
    }
    if (!(conf = exec_conf_create (job->jobspec))) {
        flux_log_error (job->h, "exec_init: exec_conf_create");
        goto err;
    }
    if (bulk_exec_aux_set (exec, "conf", conf,
                          (flux_free_f) exec_conf_destroy) < 0) {
        flux_log_error (job->h, "exec_init: bulk_exec_aux_set");
        goto err;
    }
    target_rank = idset_first (targets);
    while (target_rank != IDSET_INVALID_ID) {
        unsigned int broker_rank;
        const char *host;

        if (jobinfo_lookup_target (job, target_rank, &broker_rank, &host) < 0)
            goto err;
        if (host) {
            if (push_foreign_cmd (exec,
                                  job,
                                  target_rank,
                                  broker_rank,
                                  host) < 0) {
                flux_log_error (job->h, "exec_init: push_foreign_cmd");
                goto err;
            }
        }
        else {
            if (idset_set (native_targets, target_rank) < 0) {
                flux_log_error (job->h, "exec_init: idset_set");
                goto err;
            }
        }
        target_rank = idset_next (targets, target_rank);
    }
    if (push_native_cmd (exec, job, native_targets) < 0) {
        flux_log_error (job->h, "exec_init: push_native_cmd");
        goto err;
    }
    job->data = exec;
    idset_destroy (native_targets);
    return 1;
err:
    idset_destroy (native_targets);
    bulk_exec_destroy (exec);
    return -1;
}

static void exec_check_cb (flux_reactor_t *r, flux_watcher_t *w,
                           int revents, void *arg)
{
    struct jobinfo *job = arg;
    struct bulk_exec *exec = job->data;
    if (bulk_exec_current (exec) >= 1) {
        jobinfo_fatal_error (job, 0, "mock starting exception generated");
        flux_log (job->h,
                  LOG_DEBUG,
                  "mock exception for starting job total=%d, current=%d",
                  bulk_exec_total (exec),
                  bulk_exec_current (exec));
        flux_watcher_destroy (w);
    }
}

static int exec_start (struct jobinfo *job)
{
    struct bulk_exec *exec = job->data;

    if (strcmp (exec_mock_exception (exec), "init") == 0) {
        /* If creating an "init" mock exception, generate it and
         *  then return to simulate an exception that came in before
         *  we could actually start the job
         */
        jobinfo_fatal_error (job, 0, "mock init exception generated");
        return 0;
    }
    else if (strcmp (exec_mock_exception (exec), "starting") == 0) {
        /*  If we're going to mock an exception in "starting" phase, then
         *   set up a check watcher to cancel the job when some shells have
         *   started but (potentially) not all.
         */
        flux_reactor_t *r = flux_get_reactor (job->h);
        flux_watcher_t *w = flux_check_watcher_create (r, exec_check_cb, job);
        if (w)
            flux_watcher_start (w);
    }

    return bulk_exec_start (job->h, exec);
}

static void exec_kill_cb (flux_future_t *f, void *arg)
{
    struct jobinfo *job = arg;
    if (flux_future_get (f, NULL) < 0 && errno != ENOENT)
        flux_log_error (job->h, "%ju: exec_kill", (uintmax_t) job->id);
    jobinfo_decref (job);
    flux_future_destroy (f);
}

static int exec_kill (struct jobinfo *job, int signum)
{
    struct  bulk_exec *exec = job->data;
    flux_future_t *f = bulk_exec_kill (exec, signum);
    if (!f) {
        if (errno != ENOENT)
            flux_log_error (job->h, "%ju: bulk_exec_kill", job->id);
        return 0;
    }

    flux_log (job->h, LOG_DEBUG,
              "exec_kill: %ju: signal %d",
              (uintmax_t) job->id,
              signum);

    jobinfo_incref (job);
    if (flux_future_then (f, 3., exec_kill_cb, job) < 0) {
        flux_log_error (job->h, "%ju: exec_kill: flux_future_then", job->id);
        flux_future_destroy (f);
        return -1;
    }
    return 0;
}

static int exec_cancel (struct jobinfo *job)
{
    struct bulk_exec *exec = job->data;
    return bulk_exec_cancel (exec);
}

static int exec_cleanup (struct jobinfo *job, const struct idset *idset)
{
    /* No epilog supported */
    jobinfo_cleanup_complete (job, idset, 0);
    return 0;
}

static void exec_exit (struct jobinfo *job)
{
    struct bulk_exec *exec = job->data;
    bulk_exec_destroy (exec);
    job->data = NULL;
}

struct exec_implementation bulkexec = {
    .name =     "bulk-exec",
    .init =     exec_init,
    .exit =     exec_exit,
    .start =    exec_start,
    .kill =     exec_kill,
    .cancel =   exec_cancel,
    .cleanup =  exec_cleanup
};

/* vi: ts=4 sw=4 expandtab
 */
