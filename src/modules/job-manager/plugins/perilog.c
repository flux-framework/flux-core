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
 *    running, the prolog is terminated with SIGTERM, followed
 *    by SIGKILL
 *
 *  - The epilog is started as a result of a "finish" event,
 *    and therefore the job manager epilog is only run if
 *    job shells are actually started.
 *
 *  - Requires that a prolog and/or epilog command be configured
 *    in the [job-manager.prolog] and [job-manager.epilog]
 *    tables, e.g.
 *
 *     [job-manager.prolog]
 *     command = [ "command", "arg1", "arg2" ]
 *
 *  - The queue should be idle before unloading/reloading this
 *     plugin. Otherwise jobs may become stuck because a prolog
 *     or epilog in progress will result in a missing -finish
 *     event in the job's eventlog.
 */

#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#define EXIT_CODE(x) __W_EXITCODE(x,0)

#include <jansson.h>
#include <flux/core.h>
#include <flux/jobtap.h>

#include "src/common/libjob/job_hash.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "ccan/str/str.h"

extern char **environ;

/*  Global prolog/epilog configuration
 */
static struct perilog_conf {
    double prolog_kill_timeout;/*  Time between SIGTERM/SIGKILL for prolog  */
    flux_cmd_t *prolog_cmd;    /*  Configured prolog command                */
    flux_cmd_t *epilog_cmd;    /*  Configured epilog command                */
    zhashx_t *processes;       /*  List of outstanding perilog_proc objects */
} perilog_config;


/*  Data for a prolog/epilog process
 */
struct perilog_proc {
    flux_plugin_t *p;
    flux_jobid_t id;
    bool prolog;
    flux_subprocess_t *sp;
    flux_future_t *kill_f;
};

static struct perilog_proc * perilog_proc_create (flux_plugin_t *p,
                                                  flux_jobid_t id,
                                                  bool prolog,
                                                  flux_subprocess_t *sp)
{
    struct perilog_proc *proc = calloc (1, sizeof (*proc));
    if (proc == NULL)
        return NULL;
    proc->p = p;
    proc->id = id;
    proc->prolog = prolog;
    proc->sp = sp;
    if (zhashx_insert (perilog_config.processes, &proc->id, proc) < 0) {
        free (proc);
        return NULL;
    }
    return proc;
}

static void perilog_proc_destroy (struct perilog_proc *proc)
{
    if (proc) {
        flux_subprocess_destroy (proc->sp);
        flux_future_destroy (proc->kill_f);
        free (proc);
    }
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

static void emit_finish_event (struct perilog_proc *proc, int status)
{
    if (proc->prolog) {
        /*
         *  If prolog failed, raise job exception before prolog-finish
         *   event is emitted to ensure job isn't halfway started before
         *   the exception is raised:
         */
        if (status != 0
            && flux_jobtap_raise_exception (proc->p,
                                            proc->id,
                                            "prolog",
                                            0,
                                            "prolog failed with status=%d",
                                            status) < 0)
                flux_log_error (flux_jobtap_get_flux (proc->p),
                                "prolog-finish: flux_jobtap_raise_exception");

        if (flux_jobtap_prolog_finish (proc->p,
                                       proc->id,
                                       "job-manager.prolog",
                                       status) < 0)
            flux_log_error (flux_jobtap_get_flux (proc->p),
                            "flux_jobtap_prolog_finish: id=%ju status=%d",
                            proc->id,
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

static void completion_cb (flux_subprocess_t *sp)
{
    struct perilog_proc *proc = flux_subprocess_aux_get (sp, "perilog_proc");
    if (proc) {
        emit_finish_event (proc, flux_subprocess_status (sp));
        perilog_proc_delete (proc);
    }
}

static void state_cb (flux_subprocess_t *sp, flux_subprocess_state_t state)
{
    struct perilog_proc *proc;

    if ((state == FLUX_SUBPROCESS_FAILED
        || state == FLUX_SUBPROCESS_EXEC_FAILED)
        && (proc = flux_subprocess_aux_get (sp, "perilog_proc"))) {

        /*  If subprocess failed or execution failed, then we still
         *   must be sure to emit a finish event.
         */
        int errnum = flux_subprocess_fail_errno (sp);
        int code = EXIT_CODE(1);

        if (errnum == EPERM || errnum == EACCES)
            code = EXIT_CODE(126);
        else if (errnum == ENOENT)
            code = EXIT_CODE(127);
        else if (errnum == EHOSTUNREACH)
            code = EXIT_CODE(68);

        flux_log (flux_jobtap_get_flux (proc->p),
                  LOG_ERR,
                  "%s %s: code=%d",
                  proc->prolog ? "prolog": "epilog",
                  flux_subprocess_state_string (flux_subprocess_state (sp)),
                  code);

        emit_finish_event (proc, code);
        perilog_proc_delete (proc);
    }
}

static void io_cb (flux_subprocess_t *sp, const char *stream)
{
    struct perilog_proc *proc = flux_subprocess_aux_get (sp, "perilog_proc");
    flux_t *h = flux_jobtap_get_flux (proc->p);
    const char *s;
    int len;

    if (!(s = flux_subprocess_getline (sp, stream, &len))) {
        flux_log_error (h, "%ju: %s: %s: flux_subprocess_getline",
                        (uintmax_t) proc->id,
                        proc->prolog ? "prolog": "epilog",
                        stream);
        return;
    }
    if (len) {
        int level = LOG_INFO;
        if (streq (stream, "stderr"))
            level = LOG_ERR;
        flux_log (h,
                  level,
                  "%ju: %s: %s: %s",
                  (uintmax_t) proc->id,
                  proc->prolog ? "prolog" : "epilog",
                  stream,
                  s);
    }
}

static int run_command (flux_plugin_t *p,
                        flux_plugin_arg_t *args,
                        int prolog,
                        flux_cmd_t *cmd)
{
    flux_t *h = flux_jobtap_get_flux (p);
    struct perilog_proc *proc;
    flux_jobid_t id;
    uint32_t userid;
    flux_subprocess_t *sp = NULL;
    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_state_change = state_cb,
        .on_stdout = io_cb,
        .on_stderr = io_cb
    };
    char path[PATH_MAX + 1];
    char jobid[128];

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I s:i}",
                                "id", &id,
                                "userid", &userid) < 0) {
        flux_log_error (h, "flux_plugin_arg_unpack");
        return -1;
    }

    if (flux_job_id_encode (id, "f58", jobid, sizeof (jobid)) < 0) {
        flux_log_error (h, "flux_job_id_encode");
        return -1;
    }

    if (flux_cmd_setcwd (cmd, getcwd (path, sizeof (path))) < 0
        || flux_cmd_setenvf (cmd, 1, "FLUX_JOB_ID", "%s", jobid) < 0
        || flux_cmd_setenvf (cmd, 1, "FLUX_JOB_USERID", "%u", userid) < 0) {
        flux_log_error (h, "%s: flux_cmd_create", prolog ? "prolog" : "epilog");
        return -1;
    }

    if (!(sp = flux_rexec (h, 0, 0, cmd, &ops))) {
        flux_log_error (h, "%s: flux_rexec", prolog ? "prolog" : "epilog");
        return -1;
    }

    if (!(proc = perilog_proc_create (p, id, prolog, sp))) {
        flux_log_error (h, "%s: proc_create", prolog ? "prolog" : "epilog");
        flux_subprocess_destroy (sp);
        return -1;
    }

    if (flux_subprocess_aux_set (sp, "perilog_proc", proc, NULL) < 0) {
        flux_log_error (h, "%s: flux_subprocess_aux_set",
                        prolog ? "prolog" : "epilog");
        return -1;
    }

    if (flux_jobtap_job_aux_set (p,
                                 FLUX_JOBTAP_CURRENT_JOB,
                                 "perilog_proc",
                                 proc,
                                 NULL) < 0) {
        flux_log_error (h, "%s: flux_jobtap_job_aux_set",
                        prolog ? "prolog" : "epilog");
        return -1;
    }

    if (prolog) {
        if (flux_jobtap_job_subscribe (p, FLUX_JOBTAP_CURRENT_JOB) < 0)
            flux_log_error (h, "prolog: flux_jobtap_job_subscribe");
    }

    return 0;
}

static int run_cb (flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *args,
                   void *arg)
{
    if (perilog_config.prolog_cmd == NULL)
        return 0;

    if (run_command (p, args, 1, perilog_config.prolog_cmd) < 0) {
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
    if (perilog_config.epilog_cmd == NULL)
        return 0;

    if (run_command (p, args, 0, perilog_config.epilog_cmd) < 0) {
        flux_jobtap_raise_exception (p,
                                     FLUX_JOBTAP_CURRENT_JOB,
                                     "epilog",
                                     0,
                                     "failed to start job epilog");
        return -1;
    }
    return flux_jobtap_epilog_start (p, "job-manager.epilog");
}

static void prolog_kill_cb (flux_future_t *f, void *arg)
{
    struct perilog_proc *proc = arg;
    flux_t *h = flux_future_get_flux (f);

    if (flux_future_get (f, NULL) < 0) {
        if (errno == ETIMEDOUT) {
            flux_future_t *fkill;
            if (!(fkill = flux_subprocess_kill (proc->sp, SIGKILL)))
                flux_log_error (h,
                                "failed to send SIGKILL to prolog for %ju",
                                (uintmax_t) proc->id);
            /* Do not wait for error response */
            flux_future_destroy (fkill);
            return;
        }
        flux_log_error (h, "prolog_kill");
    }
    /*
     *  N.B.: We use flux_future_reset() on fulfilled future so that the
     *  next fulfillment will be the timeout set by the original call to
     *  flux_future_then() below. If the process is destroyed before the
     *  timer expires, then the future will also be destroyed so the
     *  timeout above won't occur.
     */
    flux_future_reset (f);
}


static int prolog_kill (struct perilog_proc *proc)
{
    flux_t *h = flux_jobtap_get_flux (proc->p);

    if (!(proc->kill_f = flux_subprocess_kill (proc->sp, SIGTERM)))
        return -1;

    if (flux_future_then (proc->kill_f,
                          perilog_config.prolog_kill_timeout,
                          prolog_kill_cb, proc) < 0) {
        flux_log_error (h, "prolog_kill: flux_future_then");
        flux_future_destroy (proc->kill_f);
        proc->kill_f = NULL;
        return -1;
    }

    return 0;
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
    if ((proc = flux_jobtap_job_aux_get (p,
                                        FLUX_JOBTAP_CURRENT_JOB,
                                        "perilog_proc"))
        && flux_subprocess_state (proc->sp) == FLUX_SUBPROCESS_RUNNING)
        return prolog_kill (proc);
    return 0;
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

/*  Parse [job-manager.prolog] and [job-manager.epilog] config
 */
static int conf_init (flux_t *h, struct perilog_conf *conf)
{
    flux_error_t error;
    json_t *prolog = NULL;
    json_t *epilog = NULL;

    memset (conf, 0, sizeof (*conf));
    conf->prolog_kill_timeout = 5.;
    if (!(conf->processes = job_hash_create ()))
        return -1;
    zhashx_set_destructor (conf->processes,
                           perilog_proc_destructor);

    if (flux_conf_unpack (flux_get_conf (h),
                          &error,
                          "{s?:{s?:{s?o s?F !} s?:{s?o !}}}",
                          "job-manager",
                          "prolog",
                            "command", &prolog,
                            "kill-timeout", &conf->prolog_kill_timeout,
                          "epilog",
                            "command", &epilog) < 0) {
        flux_log (h, LOG_ERR,
                  "prolog/epilog configuration error: %s",
                  error.text);
        return -1;
    }
    if (prolog &&
        !(conf->prolog_cmd = cmd_from_json (prolog))) {
        flux_log (h, LOG_ERR, "[job-manager.prolog] command malformed!");
        return -1;
    }
    if (epilog &&
        !(conf->epilog_cmd = cmd_from_json (epilog))) {
        flux_log (h, LOG_ERR, "[job-manager.epilog] command malformed!");
        return -1;
    }
    return 0;
}

static void free_config (struct perilog_conf *conf)
{
    flux_cmd_destroy (conf->prolog_cmd);
    flux_cmd_destroy (conf->epilog_cmd);
    zhashx_destroy (&conf->processes);
}

static const struct flux_plugin_handler tab[] = {
    { "job.state.run",       run_cb,       NULL },
    { "job.event.finish",    job_finish_cb,NULL },
    { "job.event.exception", exception_cb, NULL },
    { 0 }
};

int flux_plugin_init (flux_plugin_t *p)
{
    if (conf_init (flux_jobtap_get_flux (p), &perilog_config) < 0
        || flux_plugin_aux_set (p,
                                NULL,
                                &perilog_config,
                                (flux_free_f) free_config) < 0) {
        free_config (&perilog_config);
        return -1;
    }
    return flux_plugin_register (p, "perilog", tab);
}
