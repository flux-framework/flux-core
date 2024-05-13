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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <regex.h>
#include "src/common/libmissing/macros.h"
#define EXIT_CODE(x) __W_EXITCODE(x,0)

#include <jansson.h>
#include <flux/core.h>
#include <flux/jobtap.h>

#include "src/common/libjob/job_hash.h"
#include "src/common/libjob/idf58.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"
#include "src/broker/state_machine.h" // for STATE_CLEANUP

extern char **environ;

/*  Global prolog/epilog configuration
 */
static struct perilog_conf {
    double prolog_kill_timeout;/*  Time between SIGTERM/SIGKILL for prolog  */
    flux_cmd_t *prolog_cmd;    /*  Configured prolog command                */
    flux_cmd_t *epilog_cmd;    /*  Configured epilog command                */
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
    bool prolog;
    flux_subprocess_t *sp;
    flux_future_t *kill_f;
    flux_watcher_t *kill_timer;
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
        errno = EEXIST;
        return NULL;
    }
    return proc;
}

static void perilog_proc_destroy (struct perilog_proc *proc)
{
    if (proc) {
        flux_subprocess_destroy (proc->sp);
        flux_future_destroy (proc->kill_f);
        flux_watcher_destroy (proc->kill_timer);
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

static void emit_finish_event (struct perilog_proc *proc,
                               flux_subprocess_t *sp)
{
    int status = flux_subprocess_status (sp);
    if (proc->prolog) {
        /*
         *  If prolog failed, raise job exception before prolog-finish
         *   event is emitted to ensure job isn't halfway started before
         *   the exception is raised:
         */
        if (status != 0) {
            int code = flux_subprocess_exit_code (sp);
            int rc;
            int sig;
            char *errmsg;

            if ((sig = flux_subprocess_signaled (sp)) > 0
                || (sig = (code - 128)) > 0) {
                rc = asprintf (&errmsg,
                               "prolog killed by signal %d%s",
                               sig,
                               sig == SIGTERM ?
                               " (timeout or job canceled)" :
                               "");
            }
            else
                rc = asprintf (&errmsg,
                               "prolog exited with exit code=%d",
                               flux_subprocess_exit_code (sp));
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

static void completion_cb (flux_subprocess_t *sp)
{
    struct perilog_proc *proc = flux_subprocess_aux_get (sp, "perilog_proc");
    if (proc) {
        emit_finish_event (proc, sp);
        perilog_proc_delete (proc);
    }
}

static void state_cb (flux_subprocess_t *sp, flux_subprocess_state_t state)
{
    struct perilog_proc *proc;

    if ((state == FLUX_SUBPROCESS_FAILED)
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

        emit_finish_event (proc, sp);
        perilog_proc_delete (proc);
    }
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

static void io_cb (flux_subprocess_t *sp, const char *stream)
{
    struct perilog_proc *proc = flux_subprocess_aux_get (sp, "perilog_proc");
    flux_t *h = flux_jobtap_get_flux (proc->p);
    const char *s;
    int len;

    if ((len = flux_subprocess_getline (sp, stream, &s)) < 0) {
        flux_log_error (h, "%s: %s: %s: flux_subprocess_getline",
                        idf58 (proc->id),
                        proc->prolog ? "prolog": "epilog",
                        stream);
        return;
    }
    if (len && !perilog_log_ignore (&perilog_config, s)) {
        int level = LOG_INFO;
        if (streq (stream, "stderr"))
            level = LOG_ERR;
        flux_log (h,
                  level,
                  "%s: %s: %s: %s",
                  idf58 (proc->id),
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

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I s:i}",
                                "id", &id,
                                "userid", &userid) < 0) {
        flux_log_error (h, "flux_plugin_arg_unpack");
        return -1;
    }

    if (flux_cmd_setenvf (cmd, 1, "FLUX_JOB_ID", "%s", idf58 (id)) < 0
        || flux_cmd_setenvf (cmd, 1, "FLUX_JOB_USERID", "%u", userid) < 0) {
        flux_log_error (h, "%s: flux_cmd_create", prolog ? "prolog" : "epilog");
        return -1;
    }

    if (!(sp = flux_rexec_ex (h, "rexec", 0, 0, cmd, &ops, flux_llog, h))) {
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

    return 0;
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
    if (perilog_config.epilog_cmd || perilog_config.prolog_cmd) {
        if (flux_jobtap_job_subscribe (p, FLUX_JOBTAP_CURRENT_JOB) < 0) {
            flux_jobtap_raise_exception (p,
                                         FLUX_JOBTAP_CURRENT_JOB,
                                         "prolog",
                                         0,
                                         "failed to subscribe to job events");
            return -1;
        }
    }

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
        flux_log_error (h,
                        "%s: Failed to signal job prolog",
                        idf58 (proc->id));
    }
}

static int prolog_kill (struct perilog_proc *proc)
{
    flux_t *h = flux_jobtap_get_flux (proc->p);

    if (proc->kill_timer)
        return 0;

    if (!(proc->kill_f = flux_subprocess_kill (proc->sp, SIGTERM)))
        return -1;

    if (flux_future_then (proc->kill_f, -1., prolog_kill_cb, proc) < 0) {
        flux_log_error (h, "prolog_kill: flux_future_then");
        flux_future_destroy (proc->kill_f);
        proc->kill_f = NULL;
        return -1;
    }

    return 0;
}

static void prolog_kill_timer_cb (flux_reactor_t *r,
                                  flux_watcher_t *w,
                                  int revents,
                                  void *arg)
{
    flux_t *h;
    flux_future_t *f;
    struct perilog_proc *proc = arg;

    if (!proc || !(h = flux_jobtap_get_flux (proc->p)))
        return;

    if (!(f = flux_subprocess_kill (proc->sp, SIGKILL))) {
        flux_log_error (h,
                        "%s: failed to send SIGKILL to prolog",
                        idf58 (proc->id));
        return;
    }
    /* Do not wait for any response */
    flux_future_destroy (f);
}

static int prolog_kill_timer_start (struct perilog_proc *proc, double timeout)
{
    if (proc->kill_timer == NULL) {
        flux_t *h = flux_jobtap_get_flux (proc->p);
        flux_reactor_t *r = flux_get_reactor (h);
        proc->kill_timer = flux_timer_watcher_create (r,
                                                      timeout,
                                                      0.,
                                                      prolog_kill_timer_cb,
                                                      proc);
        if (!proc->kill_timer) {
            flux_log_error (h,
                            "%s: failed to start prolog kill timer",
                            idf58 (proc->id));
            return -1;
        }
        flux_watcher_start (proc->kill_timer);
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
        && proc->prolog
        && flux_subprocess_state (proc->sp) == FLUX_SUBPROCESS_RUNNING) {

       if (prolog_kill (proc) < 0
            || prolog_kill_timer_start (proc,
                                        perilog_config.prolog_kill_timeout) < 0)
            return -1;
    }
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

static zlistx_t *regexp_list_create ()
{
    zlistx_t *l = NULL;
    if (!(l = zlistx_new ()))
        return NULL;
    zlistx_set_destructor (l, regexp_free);
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

/*  Parse [job-manager.prolog] and [job-manager.epilog] config
 */
static int conf_init (flux_plugin_t *p, struct perilog_conf *conf)
{
    flux_t *h = flux_jobtap_get_flux (p);
    flux_error_t error;
    json_t *prolog = NULL;
    json_t *epilog = NULL;
    json_t *log_ignore = NULL;

    memset (conf, 0, sizeof (*conf));
    conf->prolog_kill_timeout = 5.;
    if (!(conf->processes = job_hash_create ()))
        return -1;
    zhashx_set_destructor (conf->processes,
                           perilog_proc_destructor);

    /*  Set up log ignore pattern list
     */
    if (!(conf->log_ignore = regexp_list_create ()))
        return -1;
    /*  Always ignore empty lines
     */
    if (regexp_list_append (conf->log_ignore, "^\\s*$", &error) < 0) {
        flux_log (h,
                  LOG_ERR,
                  "perilog: failed to pass empty pattern to log-ignore: %s",
                  error.text);
        return -1;
    }
    if (flux_conf_unpack (flux_get_conf (h),
                          &error,
                          "{s?{s?{s?o s?F !} s?{s?o !} s?{s?o}}}",
                          "job-manager",
                            "prolog",
                              "command", &prolog,
                              "kill-timeout", &conf->prolog_kill_timeout,
                            "epilog",
                              "command", &epilog,
                            "perilog",
                              "log-ignore", &log_ignore) < 0) {
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
    if (log_ignore
        && regexp_list_append_array (conf->log_ignore,
                                     log_ignore,
                                     &error) < 0) {
        flux_log (h,
                  LOG_ERR,
                  "perilog: error parsing conf.log_ignore: %s", error.text);
        return -1;
    }
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
        return -1;
    }

    return 0;
}

static void free_config (struct perilog_conf *conf)
{
    flux_future_destroy (conf->watch_f);
    flux_cmd_destroy (conf->prolog_cmd);
    flux_cmd_destroy (conf->epilog_cmd);
    zhashx_destroy (&conf->processes);
    zlistx_destroy (&conf->log_ignore);
}

static const struct flux_plugin_handler tab[] = {
    { "job.state.run",       run_cb,       NULL },
    { "job.event.finish",    job_finish_cb,NULL },
    { "job.event.exception", exception_cb, NULL },
    { 0 }
};

int flux_plugin_init (flux_plugin_t *p)
{
    if (conf_init (p, &perilog_config) < 0
        || flux_plugin_aux_set (p,
                                NULL,
                                &perilog_config,
                                (flux_free_f) free_config) < 0) {
        free_config (&perilog_config);
        return -1;
    }
    return flux_plugin_register (p, "perilog", tab);
}

// vi:ts=4 sw=4 expandtab
