/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* jobtap.c - a job manager plugin interface
 *
 *  Maintains a list of one or more job manager plugins which
 *   "tap" into job state transitions and/or events.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <unistd.h>
#include <stdarg.h>

#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "annotate.h"
#include "prioritize.h"
#include "event.h"
#include "jobtap.h"
#include "jobtap-internal.h"

#define FLUX_JOBTAP_PRIORITY_UNAVAIL INT64_C(-2)

struct jobtap_builtin {
    const char *name;
    flux_plugin_init_f init;
};

extern int default_priority_plugin_init (flux_plugin_t *p);
extern int hold_priority_plugin_init (flux_plugin_t *p);

static struct jobtap_builtin jobtap_builtins [] = {
    { "builtin.priority.default", default_priority_plugin_init },
    { "builtin.priority.hold", hold_priority_plugin_init },
    { 0 },
};

struct jobtap {
    struct job_manager *ctx;
    flux_plugin_t *plugin;
    struct job *current_job;
    char last_error [128];
};

struct jobtap *jobtap_create (struct job_manager *ctx)
{
    struct jobtap *jobtap = calloc (1, sizeof (*jobtap));
    if (!jobtap)
        return NULL;
    jobtap->ctx = ctx;
    if (jobtap_load (jobtap, "builtin.priority.default", NULL, NULL) < 0) {
        free (jobtap);
        return NULL;
    }
    return jobtap;
}

void jobtap_destroy (struct jobtap *jobtap)
{
    if (jobtap) {
        jobtap->ctx = NULL;
        flux_plugin_destroy (jobtap->plugin);
        free (jobtap);
    }
}

static const char *jobtap_plugin_name (flux_plugin_t *p)
{
    const char *name;
    if (!p)
        return "none";
    if ((name = flux_plugin_get_name (p)))
        return name;
    return "unknown";
}

static flux_plugin_arg_t *jobtap_args_create (struct jobtap *jobtap,
                                              struct job *job)
{
    flux_plugin_arg_t *args = flux_plugin_arg_create ();
    if (!args)
        return NULL;

    if (flux_plugin_arg_pack (args,
                              FLUX_PLUGIN_ARG_IN,
                              "{s:O s:I s:i s:i s:i s:I s:f}",
                              "jobspec", job->jobspec_redacted,
                              "id", job->id,
                              "userid", job->userid,
                              "urgency", job->urgency,
                              "state", job->state,
                              "priority", job->priority,
                              "t_submit", job->t_submit) < 0)
        goto error;
    /*
     *  Always start with empty OUT args. This allows unpack of OUT
     *   args to work without error, even if plugin does not set any
     *   OUT args.
     */
    if (flux_plugin_arg_set (args, FLUX_PLUGIN_ARG_OUT, "{}") < 0)
        goto error;

    return args;
error:
    flux_plugin_arg_destroy (args);
    return NULL;
}

static flux_plugin_arg_t *jobtap_args_vcreate (struct jobtap *jobtap,
                                               struct job *job,
                                               const char *fmt,
                                               va_list ap)
{
    flux_plugin_arg_t *args = jobtap_args_create (jobtap, job);
    if (!args)
        return NULL;

    if (fmt
        && flux_plugin_arg_vpack (args,
                                  FLUX_PLUGIN_ARG_IN|FLUX_PLUGIN_ARG_UPDATE,
                                  fmt, ap) < 0)
        goto error;
    return args;
error:
    flux_plugin_arg_destroy (args);
    return NULL;
}

static int jobtap_plugin_call (struct jobtap *jobtap,
                               struct job *job,
                               const char *topic,
                               flux_plugin_arg_t *args)
{
    int rc;
    jobtap->current_job = job_incref (job);
    rc = flux_plugin_call (jobtap->plugin, topic, args);
    jobtap->current_job = NULL;
    job_decref (job);
    return rc;
}

int jobtap_get_priority (struct jobtap *jobtap,
                         struct job *job,
                         int64_t *pprio)
{
    int rc = -1;
    flux_plugin_arg_t *args;
    int64_t priority = -1;

    if (!jobtap || !job || !pprio) {
        errno = EINVAL;
        return -1;
    }
    if (!jobtap->plugin) {
        *pprio = job->urgency;
        return 0;
    }
    if (!(args = jobtap_args_create (jobtap, job)))
        return -1;

    rc = jobtap_plugin_call (jobtap, job, "job.priority.get", args);

    if (rc == 1) {
        /*
         *  A priority.get callback was run. Try to unpack a new priority
         */
        if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_OUT,
                                    "{s?I}",
                                    "priority", &priority) < 0) {
            flux_log (jobtap->ctx->h, LOG_ERR,
                      "jobtap: %s: job.priority.get: arg_unpack: %s",
                      jobtap_plugin_name (jobtap->plugin),
                      flux_plugin_arg_strerror (args));
            rc = -1;
        }
        if (priority == -1) {
           /*
            *  A plugin callback was called but didn't provide a
            *   priority. This could be due to a loaded plugin that is
            *   not a priority plugin. Therefore take the default action
            *   and set priority to job->urgency.
            */
            priority = job->urgency;
        }
        else if (priority == FLUX_JOBTAP_PRIORITY_UNAVAIL) {
            /*
             *  Plugin cannot determine priority at this time. Set
             *   priority to the current job->priority so that a priority
             *   event is not generated.
             */
            priority = job->priority;
            /*
             *  A plugin cannot return an "unavailable" priority from the
             *   priority.get callback for jobs in SCHED state. Log an error
             *   in this case and make no change to priority.
             */
            if (job->state == FLUX_JOB_STATE_SCHED)
                flux_log (jobtap->ctx->h, LOG_ERR,
                          "jobtap: %s: %ju: BUG: plugin didn't return priority",
                          jobtap_plugin_name (jobtap->plugin),
                          (uintmax_t) job->id);
        }
        /*
         *   O/w, plugin provided a new priority.
         */
    }
    else if (rc == 0) {
        /*
         *  No priority.get callback was run. Enable default behavior
         *   (priority == urgency)
         */
        priority = job->urgency;
    }
    else {
        /*
         *  priority.get callback was run and failed. Log the error
         *   and return the current priority.
         */
        flux_log (jobtap->ctx->h, LOG_ERR,
                  "jobtap: %s: job.priority.get: callback failed",
                  jobtap_plugin_name (jobtap->plugin));
        priority = job->priority;
    }

    flux_plugin_arg_destroy (args);
    *pprio = priority;
    return rc;
}

int jobtap_validate (struct jobtap *jobtap,
                     struct job *job,
                     char **errp)
{
    int rc;
    flux_plugin_arg_t *args;
    const char *errmsg = NULL;

    if (!jobtap->plugin
        || !flux_plugin_match_handler (jobtap->plugin, "job.validate"))
        return 0;
    if (!(args = jobtap_args_create (jobtap, job)))
        return -1;

    rc = jobtap_plugin_call (jobtap, job, "job.validate", args);

    if (rc < 0) {
        /*
         *  Plugin callback failed, check for errmsg for this job
         *   If plugin did not provide an error message, then construct
         *   a generic error "rejected by plugin".
         */
        if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_OUT,
                                    "{s:s}",
                                    "errmsg", &errmsg) < 0)
                errmsg = "rejected by job-manager plugin";
        if ((*errp = strdup (errmsg)) == NULL)
            flux_log (jobtap->ctx->h, LOG_ERR,
                      "jobtap: %s: validate failed to capture errmsg",
                      jobtap_plugin_name (jobtap->plugin));
    }
    flux_plugin_arg_destroy (args);
    return rc;
}

int jobtap_call (struct jobtap *jobtap,
                 struct job *job,
                 const char *topic,
                 const char *fmt,
                 ...)
{
    int rc = -1;
    json_t *note = NULL;
    flux_plugin_arg_t *args;
    int64_t priority = -1;
    va_list ap;

    if (!jobtap->plugin) {
        /*
         * Default with no plugin: ensure we advance past PRIORITY state
         */
        if (job->state == FLUX_JOB_STATE_PRIORITY
           && reprioritize_job (jobtap->ctx, job, job->urgency) < 0)
            flux_log (jobtap->ctx->h, LOG_ERR,
                      "reprioritize_job: id=%ju: failed",
                      (uintmax_t) job->id);
        return 0;
    }

    va_start (ap, fmt);
    if (!(args = jobtap_args_vcreate (jobtap, job, fmt, ap))) {
        flux_log (jobtap->ctx->h, LOG_ERR,
                  "jobtap: %s: %s: %ju: failed to create plugin args",
                  jobtap_plugin_name (jobtap->plugin),
                  topic,
                  (uintmax_t) job->id);
    }
    va_end (ap);

    if (!args)
        return -1;

    rc = jobtap_plugin_call (jobtap, job, topic, args);
    if (rc < 0) {
        flux_log (jobtap->ctx->h, LOG_ERR,
                  "jobtap: %s: %s: callback returned error",
                  jobtap_plugin_name (jobtap->plugin),
                  topic);
    }
    if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_OUT,
                                "{s?I s?o}",
                                "priority", &priority,
                                "annotations", &note) < 0) {
        flux_log (jobtap->ctx->h, LOG_ERR,
                  "jobtap: %s: %s: arg_unpack: %s",
                  jobtap_plugin_name (jobtap->plugin),
                  topic,
                  flux_plugin_arg_strerror (args));
        rc = -1;
    }
    if (note != NULL) {
        /*
         *  Allow plugins to update annotations. (A failure here will be
         *   logged but not considered a fatal error)
         *
         *  In job.new callback annotations are not published because an
         *   annotation event published to the journal before the first
         *   job state event may confuse consumers (i.e. job-info).
         */
        int rc;
        if (strcmp (topic, "job.new") == 0)
            rc = annotations_update (jobtap->ctx->h, job, note);
        else
            rc = annotations_update_and_publish (jobtap->ctx, job, note);
        if (rc < 0)
            flux_log_error (jobtap->ctx->h,
                            "jobtap: %s: %s: %ju: annotations_update",
                            jobtap_plugin_name (jobtap->plugin),
                            topic,
                            (uintmax_t) job->id);
    }
    if (priority >= FLUX_JOB_PRIORITY_MIN) {
        /*
         *  Reprioritize job if plugin returned a priority.
         *   Note: reprioritize_job() is a no-op if job is not in
         *         PRIORITY or SCHED state)
         */
        if (reprioritize_job (jobtap->ctx, job, priority) < 0) {
            flux_log_error (jobtap->ctx->h,
                            "jobtap: %s: reprioritize_job",
                            jobtap_plugin_name (jobtap->plugin));
        }
    }
    else if (job->state == FLUX_JOB_STATE_PRIORITY && priority == -1) {
        /*
         *  Plugin didn't return a priority value (not even
         *   FLUX_JOBTAP_PRIORITY_UNAVAIL). Take default action
         *   to prevent job from being stuck in PRIORITY state when
         *   a non-priority plugin is loaded.
         */
        if (reprioritize_job (jobtap->ctx, job, job->urgency) < 0) {
            flux_log_error (jobtap->ctx->h,
                            "jobtap: setting priority to urgency failed");
        }
    }
    /*  else: FLUX_JOBTAP_PRIORITY_UNAVAIL, job cannot yet be assigned a
     *   priority. This is a fall-through condiition. A job in PRIORITY
     *   state will stay there until the plugin actively calls
     *   flux_jobtap_reprioritize_job()
     */
    flux_plugin_arg_destroy (args);
    return rc;
}

static int jobtap_load_builtin (flux_plugin_t *p,
                                const char *name)
{
    struct jobtap_builtin *builtin = jobtap_builtins;

    while (builtin && builtin->name) {
        if (strcmp (name, builtin->name) == 0) {
            if (flux_plugin_set_name (p, builtin->name) < 0)
                return -1;
            return (*builtin->init) (p);
        }
        builtin++;
    }

    errno = ENOENT;
    return -1;
}

int jobtap_load (struct jobtap *jobtap,
                 const char *path,
                 json_t *conf,
                 jobtap_error_t *errp)
{
    flux_plugin_t *p = NULL;
    char *conf_str = NULL;

    if (errp)
        memset (errp->text, 0, sizeof (errp->text));

    if (conf && !json_is_null (conf)) {
        if (!json_is_object (conf)) {
            errno = EINVAL;
            snprintf (errp->text, sizeof (errp->text), "%s",
                      "jobptap: plugin conf must be a JSON object");
            goto error;
        }
        if (!(conf_str = json_dumps (conf, 0))) {
            errno = ENOMEM;
            snprintf (errp->text, sizeof (errp->text), "%s: %s",
                      "jobtap: json_dumps(conf) failed", strerror (errno));
            goto error;
        }
    }

    /*  Special "none" value leaves no plugin loaded
     */
    if (strcmp (path, "none") == 0) {
        flux_plugin_destroy (jobtap->plugin);
        jobtap->plugin = NULL;
        return 0;
    }

    if (!(p = flux_plugin_create ())
        || flux_plugin_aux_set (p, "flux::jobtap", jobtap, NULL) < 0)
        goto error;
    if (strncmp (path, "builtin.", 8) == 0) {
        if (jobtap_load_builtin (p, path) < 0)
            goto error;
        goto done;
    }
    if (conf_str) {
        int rc = flux_plugin_set_conf (p, conf_str);
        free (conf_str);
        if (rc < 0)
            goto error;
    }
    flux_plugin_set_flags (p, FLUX_PLUGIN_RTLD_NOW);
    if (flux_plugin_load_dso (p, path) < 0)
        goto error;
    /*
     *  A jobtap plugin must set a name, error out if not:
     */
    if (strcmp (flux_plugin_get_name (p), path) == 0) {
        snprintf (errp->text, sizeof (errp->text), "%s",
                  "Plugin did not set name in flux_plugin_init");
        goto error;
    }
done:
    flux_plugin_destroy (jobtap->plugin);
    jobtap->plugin = p;
    return 0;
error:
    if (errp && errp->text[0] == '\0')
        strncpy (errp->text,
                 flux_plugin_strerror (p),
                 sizeof (errp->text) - 1);
    flux_plugin_destroy (p);
    return -1;
}

static void jobtap_handle_load_req (struct job_manager *ctx,
                                    const flux_msg_t *msg,
                                    const char *path,
                                    json_t *conf)
{
    char *prev = NULL;
    jobtap_error_t error;
    const char *errstr = NULL;
    struct job *job = NULL;
    zlistx_t *jobs;

    if (!(prev = strdup (jobtap_plugin_name (ctx->jobtap->plugin))))
        goto error;
    if (jobtap_load (ctx->jobtap, path, conf, &error) < 0) {
        errstr = error.text;
        goto error;
    }

    /*  Make plugin aware of all active jobs via job.new callback
     */
    jobs = zhashx_values (ctx->active_jobs);
    job = zlistx_first (jobs);
    while (job) {
        (void) jobtap_call (ctx->jobtap, job, "job.new", NULL);

        /*  If job is in DEPEND state then there may be pending dependencies.
         *  Notify plugin of the DEPEND state assuming it needs to create
         *   some state in order to resolve the dependency.
         */
        if (job->state == FLUX_JOB_STATE_DEPEND)
            (void) jobtap_call (ctx->jobtap, job, "job.state.depend", NULL);

        job = zlistx_next (jobs);
    }
    zlistx_destroy (&jobs);

    /* Now schedule reprioritize of all jobs
     */
    if (reprioritize_all (ctx) < 0) {
        errstr = "jobtap: plugin loaded but failed to reprioritize all jobs";
        goto error;
    }
    if (flux_respond_pack (ctx->h,
                           msg,
                           "{s:[s] s:[s]}",
                           "plugins",
                           jobtap_plugin_name (ctx->jobtap->plugin),
                           "previous",
                           prev ? prev : "none") < 0)
        flux_log_error (ctx->h, "jobtap_handle_load_req: flux_respond");
    free (prev);
    return;
error:
    free (prev);
    if (flux_respond_error (ctx->h, msg, errno, errstr) < 0)
        flux_log_error (ctx->h, "jobtap_handler: flux_respond_error");
}

static void jobtap_handle_list_req (flux_t *h,
                                    struct jobtap *jobtap,
                                    const flux_msg_t *msg)
{
    if (flux_respond_pack (h, msg,
                           "{ s:[s] }",
                           "plugins",
                           jobtap_plugin_name (jobtap->plugin)) < 0)
        flux_log_error (h, "jobtap_handle_list: flux_respond");
}

void jobtap_handler (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg)
{
    struct job_manager *ctx = arg;
    const char *path = NULL;
    int query_only = 0;
    json_t *conf = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s?s s?o s?b}",
                             "load", &path,
                             "conf", &conf,
                             "query_only", &query_only) < 0) {
        if (flux_respond_error (h, msg, EPROTO, NULL) < 0)
            flux_log_error (h, "jobtap_handler: flux_respond_error");
        return;
    }
    if (query_only)
        jobtap_handle_list_req (h, ctx->jobtap, msg);
    else
        jobtap_handle_load_req (ctx, msg, path, conf);
}

flux_t *flux_jobtap_get_flux (flux_plugin_t *p)
{
    struct jobtap *jobtap = NULL;

    if (p == NULL
        || !(jobtap = flux_plugin_aux_get (p, "flux::jobtap"))
        || !jobtap->ctx) {
        errno = EINVAL;
        return NULL;
    }
    return jobtap->ctx->h;
}

static int build_jobtap_topic (flux_plugin_t *p,
                               const char *method,
                               char *buf,
                               int len)
{
    const char *name = jobtap_plugin_name (p);

    /*  Plugin name must be set before calling service_register:
     *  (by default, path loaded plugins have their name set to the path.
     *   detect that here with strchr())
     */
    if (name == NULL || strchr (name, '/')) {
        errno = EINVAL;
        return -1;
    }
    if (snprintf (buf,
                  len,
                  "job-manager.%s%s%s",
                  name,
                  method ? "." : "",
                  method ? method : "") >= len) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_jobtap_service_register (flux_plugin_t *p,
                                  const char *method,
                                  flux_msg_handler_f cb,
                                  void *arg)
{
    struct flux_match match = FLUX_MATCH_REQUEST;
    flux_msg_handler_t *mh;
    char topic[1024];
    flux_t *h;

    if (!(h = flux_jobtap_get_flux (p))
        || build_jobtap_topic (p, method, topic, sizeof (topic)) < 0)
        return -1;

    match.topic_glob = topic;
    if (!(mh = flux_msg_handler_create (h, match, cb, arg)))
        return -1;

    if (flux_plugin_aux_set (p,
                             NULL,
                             mh,
                             (flux_free_f) flux_msg_handler_destroy) < 0) {
        flux_msg_handler_destroy (mh);
        return -1;
    }
    flux_msg_handler_start (mh);
    flux_log (h, LOG_DEBUG, "jobtap plugin %s registered method %s",
              jobtap_plugin_name (p),
              topic);
    return 0;
}

int flux_jobtap_reprioritize_all (flux_plugin_t *p)
{
    struct jobtap *jobtap = flux_plugin_aux_get (p, "flux::jobtap");
    if (!jobtap) {
        errno = EINVAL;
        return -1;
    }
    return reprioritize_all (jobtap->ctx);
}

int flux_jobtap_reprioritize_job (flux_plugin_t *p,
                                  flux_jobid_t id,
                                  unsigned int priority)
{
    struct jobtap *jobtap = flux_plugin_aux_get (p, "flux::jobtap");
    if (!jobtap) {
        errno = EINVAL;
        return -1;
    }
    return reprioritize_id (jobtap->ctx, id, priority);
}

int flux_jobtap_priority_unavail (flux_plugin_t *p, flux_plugin_arg_t *args)
{
    struct jobtap *jobtap = flux_plugin_aux_get (p, "flux::jobtap");
    if (!jobtap) {
        errno = EINVAL;
        return -1;
    }
    /* Still todo: check valid state, etc.
     */
    return flux_plugin_arg_pack (args,
                                 FLUX_PLUGIN_ARG_OUT|FLUX_PLUGIN_ARG_UPDATE,
                                 "{s:I}",
                                 "priority", FLUX_JOBTAP_PRIORITY_UNAVAIL);
}

int flux_jobtap_reject_job (flux_plugin_t *p,
                            flux_plugin_arg_t *args,
                            const char *fmt,
                            ...)
{
    char errmsg [1024];
    int len = sizeof (errmsg);
    int n;

    if (fmt) {
        va_list ap;
        va_start (ap, fmt);
        n = vsnprintf (errmsg, sizeof (errmsg), fmt, ap);
        va_end (ap);
    }
    else {
        n = snprintf (errmsg,
                      sizeof (errmsg),
                      "rejected by job-manager plugin '%s'",
                      jobtap_plugin_name (p));
    }
    if (n >= len) {
        errmsg[len - 1] = '\0';
        errmsg[len - 2] = '+';
    }
    if (flux_plugin_arg_pack (args,
                              FLUX_PLUGIN_ARG_OUT|FLUX_PLUGIN_ARG_UPDATE,
                              "{s:s}",
                              "errmsg", errmsg) < 0) {
        flux_t *h = flux_jobtap_get_flux (p);
        if (h)
            flux_log_error (h, "flux_jobtap_reject_job: failed to pack error");
    }
    return -1;
}

static struct job *lookup_job (struct job_manager *ctx, flux_jobid_t id)
{
    struct job *job = zhashx_lookup (ctx->active_jobs, &id);
    if (!job)
        errno = ENOENT;
    return job;
}


static int emit_dependency_event (flux_plugin_t *p,
                                  flux_jobid_t id,
                                  const char *event,
                                  const char *description)
{
    int flags = 0;
    struct job *job;

    struct jobtap *jobtap = flux_plugin_aux_get (p, "flux::jobtap");
    if (!jobtap) {
        errno = EINVAL;
        return -1;
    }
    if (!(job = lookup_job (jobtap->ctx, id)))
        return -1;
    if (job->state != FLUX_JOB_STATE_DEPEND) {
        errno = EINVAL;
        return -1;
    }
    if (!job_dependency_event_valid (job, event, description))
       return -1;
    return event_job_post_pack (jobtap->ctx->event,
                                job,
                                event,
                                flags,
                                "{s:s}",
                                "description", description);
}

int flux_jobtap_dependency_add (flux_plugin_t *p,
                                flux_jobid_t id,
                                const char *description)
{
    return emit_dependency_event (p, id, "dependency-add", description);
}

int flux_jobtap_dependency_remove (flux_plugin_t *p,
                                   flux_jobid_t id,
                                   const char *description)
{
    return emit_dependency_event (p, id, "dependency-remove", description);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

