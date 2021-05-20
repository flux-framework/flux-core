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
#include <fnmatch.h>

#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/iterators.h"
#include "src/common/libutil/errno_safe.h"

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
    char *searchpath;
    zlistx_t *plugins;
    struct job *current_job;
    char last_error [128];
};

struct dependency {
    bool add;
    char *description;
};

static int job_emit_pending_dependencies (struct jobtap *jobtap,
                                          struct job *job);


static int errprintf (jobtap_error_t *errp, const char *fmt, ...)
{
    va_list ap;
    int n;
    int saved_errno = errno;

    if (!errp)
        return -1;

    va_start (ap, fmt);
    n = vsnprintf (errp->text, sizeof (errp->text), fmt, ap);
    va_end (ap);
    if (n > sizeof (errp->text))
        errp->text[sizeof (errp->text) - 2] = '+';

    errno = saved_errno;
    return -1;
}

static struct dependency * dependency_create (bool add,
                                              const char *description)
{
    struct dependency *dp = calloc (1, sizeof (*dp));
    if (!dp || !(dp->description = strdup (description))) {
        free (dp);
        return NULL;
    }
    dp->add = add;
    return dp;
}

static void dependency_destroy (void **item)
{
    if (*item) {
        struct dependency *dp = *item;
        int saved_errno = errno;
        free (dp->description);
        free (dp);
        *item = NULL;
        errno = saved_errno;
    }
}

/*  zlistx_t plugin destructor */
static void plugin_destroy (void **item)
{
    if (item) {
        flux_plugin_t *p = *item;
        flux_plugin_destroy (p);
        *item = NULL;
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


static flux_plugin_t * jobtap_load_plugin (struct jobtap *jobtap,
                                           const char *path,
                                           json_t *conf,
                                           jobtap_error_t *errp)
{
    struct job_manager *ctx = jobtap->ctx;
    flux_plugin_t *p = NULL;
    flux_plugin_arg_t *args;
    zlistx_t *jobs;
    struct job *job;

    if (!(p = jobtap_load (jobtap, path, conf, errp)))
        goto error;

    /*  Make plugin aware of all active jobs via job.new callback
     */
    if (!(jobs = zhashx_values (ctx->active_jobs))) {
        errprintf (errp, "zhashx_values() failed");
        goto error;
    }
    job = zlistx_first (jobs);
    while (job) {
        if (!(args = jobtap_args_create (jobtap, job))) {
            errprintf (errp, "Failed to create args for job");
            goto error;
        }

        /*  Notify this plugin of all jobs via `job.new` callback. */
        (void) flux_plugin_call (p, "job.new", args);

        /*  If job is in DEPEND state then there may be pending dependencies.
         *  Notify plugin of the DEPEND state assuming it needs to create
         *   some state in order to resolve the dependency.
         */
        if (job->state == FLUX_JOB_STATE_DEPEND)
            (void) flux_plugin_call (p, "job.state.depend", args);

        flux_plugin_arg_destroy (args);
        job = zlistx_next (jobs);
    }
    zlistx_destroy (&jobs);

    /* Now schedule reprioritize of all jobs
     */
    if (reprioritize_all (ctx) < 0) {
        errprintf (errp,
                   "%s loaded but unable to reprioritize jobs",
                   flux_plugin_get_name (p));
    }
    return p;
error:
    flux_plugin_destroy (p);
    return NULL;
}

static bool isa_glob (const char *s)
{
    if (strchr (s, '*') || strchr (s, '?') || strchr (s, '['))
        return true;
    return false;
}

static int jobtap_remove (struct jobtap *jobtap,
                          const char *arg,
                          jobtap_error_t *errp)
{
    int count = 0;
    bool isglob = isa_glob (arg);
    bool all = strcmp (arg, "all") == 0;

    flux_plugin_t *p = zlistx_first (jobtap->plugins);
    while (p) {
        const char *name = jobtap_plugin_name (p);
        if (all
            || (isglob && fnmatch (arg, name, 0) == 0)
            || strcmp (arg, name) == 0) {
            zlistx_detach_cur (jobtap->plugins);
            flux_plugin_destroy (p);
            count++;
        }
        p = zlistx_next (jobtap->plugins);
    }
    if (count == 0 && !all) {
        errno = ENOENT;
        return errprintf (errp, "Failed to find plugin to remove");
    }
    return count;
}

static int jobtap_conf_entry (struct jobtap *jobtap,
                              int index,
                              json_t *entry,
                              jobtap_error_t *errp)
{
    json_error_t json_err;
    jobtap_error_t jobtap_err;
    const char *load = NULL;
    const char *remove = NULL;
    json_t *conf = NULL;

    if (json_unpack_ex (entry, &json_err, 0,
                        "{s?:s s?:o s?:s}",
                        "load", &load,
                        "conf", &conf,
                        "remove", &remove) < 0) {
        return errprintf (errp,
                          "[job-manager.plugins][%d]: %s",
                          index,
                          json_err.text);
    }
    if (remove && jobtap_remove (jobtap, remove, &jobtap_err) < 0) {
        return errprintf (errp,
                          "[job-manager.plugins][%d]: remove %s: %s",
                          index,
                          remove,
                          jobtap_err.text);
    }
    if (load && jobtap_load_plugin (jobtap,
                                    load,
                                    conf,
                                    &jobtap_err) < 0) {
        return errprintf (errp,
                          "[job-manager.plugins][%d]: load %s: %s",
                          index,
                          load,
                          jobtap_err.text);
    }
    return 0;
}

static int jobtap_parse_config (struct jobtap *jobtap,
                                const flux_conf_t *conf,
                                jobtap_error_t *errp)
{
    json_t *plugins = NULL;
    flux_conf_error_t error;
    json_t *entry;
    int i;

    if (!conf)
        return errprintf (errp, "conf object can't be NULL");

    if (flux_conf_unpack (conf,
                          &error,
                          "{s?:{s?:o}}",
                          "job-manager",
                            "plugins", &plugins) < 0) {
        return errprintf (errp,
                          "[job-manager.plugins]: unpack error: %s",
                          error.errbuf);
    }

    if (!plugins)
        return 0;

    if (!json_is_array (plugins)) {
        return errprintf (errp,
                         "[job-manager.plugins] config must be an array");
    }

    json_array_foreach (plugins, i, entry) {
        if (jobtap_conf_entry (jobtap, i, entry, errp) < 0)
            return -1;
    }
    return 0;
}

struct jobtap *jobtap_create (struct job_manager *ctx)
{
    const char *path;
    jobtap_error_t error;
    struct jobtap *jobtap = calloc (1, sizeof (*jobtap));
    if (!jobtap)
        return NULL;
    jobtap->ctx = ctx;
    if ((path = flux_conf_builtin_get ("jobtap_pluginpath", FLUX_CONF_AUTO))
        && !(jobtap->searchpath = strdup (path)))
        goto error;
    if (!(jobtap->plugins = zlistx_new ())) {
        errno = ENOMEM;
        goto error;
    }
    zlistx_set_destructor (jobtap->plugins, plugin_destroy);

    if (jobtap_load (jobtap, "builtin.priority.default", NULL, NULL) < 0)
        goto error;

    if (jobtap_parse_config (jobtap, flux_get_conf (ctx->h), &error) < 0) {
        flux_log (ctx->h, LOG_ERR, "%s", error.text);
        goto error;
    }

    return jobtap;
error:
    jobtap_destroy (jobtap);
    return NULL;
}

void jobtap_destroy (struct jobtap *jobtap)
{
    if (jobtap) {
        int saved_errno = errno;
        zlistx_destroy (&jobtap->plugins);
        jobtap->ctx = NULL;
        free (jobtap->searchpath);
        free (jobtap);
        errno = saved_errno;
    }
}

static int jobtap_topic_match_count (struct jobtap *jobtap,
                                     const char *topic)
{
    int count = 0;
    flux_plugin_t *p = zlistx_first (jobtap->plugins);
    while (p) {
        if (flux_plugin_match_handler (p, topic))
            count++;
        p = zlistx_next (jobtap->plugins);
    }
    return count;
}

static int jobtap_stack_call (struct jobtap *jobtap,
                              struct job *job,
                              const char *topic,
                              flux_plugin_arg_t *args)
{
    int retcode = 0;
    flux_plugin_t *p = zlistx_first (jobtap->plugins);

    jobtap->current_job = job_incref (job);
    while (p) {
        int rc = flux_plugin_call (p, topic, args);
        if (rc < 0)  {
            flux_log (jobtap->ctx->h, LOG_DEBUG,
                      "jobtap: %s: %s: rc=%d",
                      jobtap_plugin_name (p),
                      topic,
                      rc);
            retcode = -1;
            break;
        }
        retcode += rc;
        p = zlistx_next (jobtap->plugins);
    }
    jobtap->current_job = NULL;
    job_decref (job);
    return retcode;
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

    /*  Skip if no jobtap.priority.get handlers are active.
     *   This avoids unnecessarily creating a flux_plugin_arg_t object.
     */
    if (jobtap_topic_match_count (jobtap, "job.priority.get")  == 0) {
        *pprio = job->urgency;
        return 0;
    }
    if (!(args = jobtap_args_create (jobtap, job)))
        return -1;

    rc = jobtap_stack_call (jobtap, job, "job.priority.get", args);

    if (rc >= 1) {
        /*
         *  A priority.get callback was run. Try to unpack a new priority
         */
        if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_OUT,
                                    "{s?I}",
                                    "priority", &priority) < 0) {
            flux_log (jobtap->ctx->h, LOG_ERR,
                      "jobtap: job.priority.get: arg_unpack: %s",
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
                          "jobtap: %ju: BUG: plugin didn't return priority",
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
                  "jobtap: job.priority.get: callback failed");
        priority = job->priority;
    }

    flux_plugin_arg_destroy (args);
    *pprio = priority;
    return rc;
}

static void error_asprintf (struct jobtap *jobtap,
                            struct job *job,
                            char **errp,
                            const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    if (vasprintf (errp, fmt, ap) < 0)
        flux_log_error (jobtap->ctx->h,
                        "id=%ju: failed to create error string: fmt=%s",
                        (uintmax_t) job->id, fmt);
    va_end (ap);
}

int jobtap_validate (struct jobtap *jobtap,
                     struct job *job,
                     char **errp)
{
    int rc;
    flux_plugin_arg_t *args;
    const char *errmsg = NULL;

    if (jobtap_topic_match_count (jobtap, "job.validate") == 0)
        return 0;
    if (!(args = jobtap_args_create (jobtap, job)))
        return -1;

    rc = jobtap_stack_call (jobtap, job, "job.validate", args);

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
                      "jobtap: validate failed to capture errmsg");
    }
    flux_plugin_arg_destroy (args);
    return rc;
}

static int jobtap_check_dependency (struct jobtap *jobtap,
                                    struct job *job,
                                    flux_plugin_arg_t *args,
                                    int index,
                                    json_t *entry,
                                    char **errp)
{
    int rc = -1;
    char topic [128];
    const char *scheme = NULL;

    if (json_unpack (entry, "{s:s}", "scheme", &scheme) < 0
        || scheme == NULL) {
        error_asprintf (jobtap, job, errp,
                        "dependency[%d] missing string scheme",
                        index);
        return -1;
    }

    if (snprintf (topic,
                  sizeof (topic),
                  "job.dependency.%s",
                  scheme) > sizeof (topic)) {
        error_asprintf (jobtap, job, errp,
                        "rejecting absurdly long dependency scheme: %s",
                        scheme);
        return -1;
    }

    if (flux_plugin_arg_pack (args,
                              FLUX_PLUGIN_ARG_IN|FLUX_PLUGIN_ARG_UPDATE,
                              "{s:O}",
                              "dependency", entry) < 0
        || flux_plugin_arg_set (args, FLUX_PLUGIN_ARG_OUT, "{}") < 0) {
        flux_log_error (jobtap->ctx->h,
                        "jobtap_check_depedency: failed to prepare args");
        return -1;
    }

    rc = jobtap_stack_call (jobtap, job, topic, args);
    if (rc == 0) {
        /*  No handler for job.dependency.<scheme>. return an error.
         */
        error_asprintf (jobtap, job, errp,
                        "dependency scheme \"%s\" not supported",
                        scheme);
        rc = -1;
    }
    else if (rc < 0) {
        /*
         *  Plugin callback failed, check for errmsg for this job
         *   If plugin did not provide an error message, then construct
         *   a generic error "rejected by plugin".
         */
        const char *errmsg;
        if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_OUT,
                                    "{s:s}",
                                    "errmsg", &errmsg) < 0) {
                errmsg = "rejected by job-manager dependency plugin";
        }
        error_asprintf (jobtap, job, errp, "%s", errmsg);
    }
    return rc;
}

static int dependencies_unpack (struct jobtap * jobtap,
                                struct job * job,
                                char **errp,
                                json_t **resultp)
{
    json_t *dependencies = NULL;
    json_error_t error;

    if (json_unpack_ex (job->jobspec_redacted, &error, 0,
                        "{s:{s?{s?o}}}",
                        "attributes",
                        "system",
                        "dependencies", &dependencies) < 0) {
        error_asprintf (jobtap, job, errp,
                        "unable to unpack dependencies: %s",
                        error.text);
        return -1;
    }

    if (!dependencies)
        return 0;

    if (!json_is_array (dependencies)) {
        error_asprintf (jobtap, job, errp,
                        "dependencies object must be an array");
        return -1;
    }

    if (json_array_size (dependencies) == 0)
        return 0;

    *resultp = dependencies;
    return 0;
}

int jobtap_check_dependencies (struct jobtap *jobtap,
                               struct job *job,
                               char **errp)
{
    int rc = -1;
    flux_plugin_arg_t *args = NULL;
    json_t *dependencies = NULL;
    json_t *entry;
    size_t index;

    if ((rc = dependencies_unpack (jobtap, job, errp, &dependencies)) < 0
        || dependencies == NULL)
        return rc;

    if (!(args = jobtap_args_create (jobtap, job))) {
        error_asprintf (jobtap, job, errp,
                        "jobtap_check_dependencies: failed to create args");
        return -1;
    }

    json_array_foreach (dependencies, index, entry) {
        rc = jobtap_check_dependency (jobtap, job, args, index, entry, errp);
        if (rc < 0)
           goto out;
    }
    rc = 0;
out:
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

    if (job->state == FLUX_JOB_STATE_DEPEND) {
        /*  Ensure any pending dependencies are emitted before calling
         *   into job.state.depend callback to prevent the depend event
         *   itself when not all dependencies are resolved.
         */
        if (job_emit_pending_dependencies (jobtap, job) < 0)
            return -1;
    }

    if (jobtap_topic_match_count (jobtap, topic) == 0) {
        /*
         *  ensure job advances past PRIORITY state at job.state.priority
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
                  "jobtap: %s: %ju: failed to create plugin args",
                  topic,
                  (uintmax_t) job->id);
    }
    va_end (ap);

    if (!args)
        return -1;

    rc = jobtap_stack_call (jobtap, job, topic, args);
    if (rc < 0) {
        flux_log (jobtap->ctx->h, LOG_ERR,
                  "jobtap: %s: callback returned error",
                  topic);
    }
    if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_OUT,
                                "{s?I s?o}",
                                "priority", &priority,
                                "annotations", &note) < 0) {
        flux_log (jobtap->ctx->h, LOG_ERR,
                  "jobtap: %s: arg_unpack: %s",
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
                            "jobtap: %s: %ju: annotations_update",
                            topic,
                            (uintmax_t) job->id);
    }
    if (priority >= FLUX_JOB_PRIORITY_MIN) {
        /*
         *  Reprioritize job if plugin returned a priority.
         *   Note: reprioritize_job() is a no-op if job is not in
         *         PRIORITY or SCHED state)
         */
        if (reprioritize_job (jobtap->ctx, job, priority) < 0)
            flux_log_error (jobtap->ctx->h, "jobtap: reprioritize_job");
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

/*  Return 1 if either searchpath is NULL, or path starts with '/' or './'.
 */
static int no_searchpath (const char *searchpath, const char *path)
{
    return (!searchpath
            || path[0] == '/'
            || (path[0] == '.' && path[1] == '/'));
}

static void item_free (void **item )
{
    if (*item) {
        free (*item);
        *item = NULL;
    }
}

static zlistx_t *path_list (const char *searchpath, const char *path)
{
    char *copy;
    char *str;
    char *dir;
    char *s;
    char *sp = NULL;
    zlistx_t *l = zlistx_new ();

    if (!l || !(copy = strdup (searchpath)))
        return NULL;
    str = copy;

    zlistx_set_destructor (l, item_free);

    while ((dir = strtok_r (str, ":", &sp))) {
        if (asprintf (&s, "%s/%s", dir, path) < 0)
            goto error;
        if (!zlistx_add_end (l, s))
            goto error;
        str = NULL;
    }
    free (copy);
    return l;
error:
    ERRNO_SAFE_WRAP (free, copy);
    ERRNO_SAFE_WRAP (zlistx_destroy, &l);
    return NULL;
}

int jobtap_plugin_load_first (struct jobtap *jobtap,
                              flux_plugin_t *p,
                              const char *path,
                              json_t *conf,
                              jobtap_error_t *errp)
{
    bool found = false;
    zlistx_t *l;
    char *fullpath;

    if (no_searchpath (jobtap->searchpath, path))
        return flux_plugin_load_dso (p, path);

    if (!(l = path_list (jobtap->searchpath, path)))
        return -1;

    fullpath = zlistx_first (l);
    while (fullpath) {
        int rc = flux_plugin_load_dso (p, fullpath);
        if (rc == 0) {
            found = true;
            break;
        }
        if (rc < 0 && errno != ENOENT) {
            return errprintf (errp,
                              "%s: %s",
                              fullpath,
                              flux_plugin_strerror (p));
        }
        fullpath = zlistx_next (l);
    }
    zlistx_destroy (&l);
    if (!found) {
        errno = ENOENT;
        return errprintf (errp, "%s: No such plugin found", path);
    }
    return 0;
}

flux_plugin_t * jobtap_load (struct jobtap *jobtap,
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
            errprintf (errp, "jobptap: plugin conf must be a JSON object");
            goto error;
        }
        if (!(conf_str = json_dumps (conf, 0))) {
            errno = ENOMEM;
            errprintf (errp, "%s: %s",
                      "jobtap: json_dumps(conf) failed",
                      strerror (errno));
            goto error;
        }
    }

    if (!(p = flux_plugin_create ())
        || flux_plugin_aux_set (p, "flux::jobtap", jobtap, NULL) < 0)
        goto error;
    if (conf_str) {
        int rc = flux_plugin_set_conf (p, conf_str);
        free (conf_str);
        if (rc < 0)
            goto error;
    }
    if (strncmp (path, "builtin.", 8) == 0) {
        if (jobtap_load_builtin (p, path) < 0)
            goto error;
    }
    else {
        flux_plugin_set_flags (p, FLUX_PLUGIN_RTLD_NOW);
        if (jobtap_plugin_load_first (jobtap, p, path, conf, errp) < 0)
            goto error;
        /*
         *  A jobtap plugin must set a name, error out if not:
         */
        if (strcmp (flux_plugin_get_name (p), path) == 0) {
            errprintf (errp, "Plugin did not set name in flux_plugin_init");
            goto error;
        }
    }
    if (!zlistx_add_end (jobtap->plugins, p)) {
        errprintf (errp, "Out of memory adding plugin to list");
        errno = ENOMEM;
        goto error;
    }
    return p;
error:
    if (errp && errp->text[0] == '\0')
        strncpy (errp->text,
                 flux_plugin_strerror (p),
                 sizeof (errp->text) - 1);
    flux_plugin_destroy (p);
    return NULL;
}

static int jobtap_handle_remove_req (struct job_manager *ctx,
                                     const flux_msg_t *msg,
                                     const char *arg)
{
    jobtap_error_t error;
    if (jobtap_remove (ctx->jobtap, arg, &error) < 0) {
        if (flux_respond_error (ctx->h,
                                msg,
                                errno ? errno : EINVAL,
                                error.text) < 0)
            flux_log_error (ctx->h,
                            "jobtap_handle_remove_req: flux_respond_error");
        return -1;
    }
    return 0;
}

static int jobtap_handle_load_req (struct job_manager *ctx,
                                   const flux_msg_t *msg,
                                   const char *path,
                                   json_t *conf)
{
    jobtap_error_t error;
    flux_plugin_t *p = NULL;

    if (!(p = jobtap_load_plugin (ctx->jobtap, path, conf, &error))) {
        if (flux_respond_error (ctx->h,
                                msg,
                                errno ? errno : EINVAL,
                                error.text) < 0)
            flux_log_error (ctx->h, "jobtap_handler: flux_respond_error");
        return -1;
    }
    return 0;
}

static json_t *jobtap_plugin_list (struct jobtap *jobtap)
{
    flux_plugin_t *p;
    json_t *result = json_array ();
    if (result == NULL)
        return NULL;
    p = zlistx_first (jobtap->plugins);
    while (p) {
        json_t *o = json_string (jobtap_plugin_name (p));
        if (o == NULL)
            goto error;
        if (json_array_append_new (result, o) < 0) {
            json_decref (o);
            goto error;
        }
        p = zlistx_next (jobtap->plugins);
    }
    return result;
error:
    json_decref (result);
    return NULL;
}

static void jobtap_handle_list_req (flux_t *h,
                                    struct jobtap *jobtap,
                                    const flux_msg_t *msg)
{
    json_t *o = jobtap_plugin_list (jobtap);
    if (o == NULL)
        flux_respond_error (h, msg, ENOMEM, "Failed to create plugin list");
    else if (flux_respond_pack (h, msg,
                                "{ s:o }",
                                "plugins", o) < 0)
        flux_log_error (h, "jobtap_handle_list: flux_respond");
}

void jobtap_handler (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg)
{
    struct job_manager *ctx = arg;
    const char *path = NULL;
    const char *remove = NULL;
    int query_only = 0;
    json_t *conf = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s?s s?o s?s s?b}",
                             "load", &path,
                             "conf", &conf,
                             "remove", &remove,
                             "query_only", &query_only) < 0) {
        if (flux_respond_error (h, msg, EPROTO, NULL) < 0)
            flux_log_error (h, "jobtap_handler: flux_respond_error");
        return;
    }
    if (query_only) {
        jobtap_handle_list_req (h, ctx->jobtap, msg);
        return;
    }
    if (remove && jobtap_handle_remove_req (ctx, msg, remove) < 0)
        return;
    if (path && jobtap_handle_load_req (ctx, msg, path, conf) < 0)
        return;
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "jobtap_handler: flux_respond");
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

static void zlist_free (void *arg)
{
    if (arg)
        zlistx_destroy ((zlistx_t **) &arg);
}

static int add_pending_dependency (struct job *job,
                                   bool add,
                                   const char *description)
{
    struct dependency *dp = NULL;
    zlistx_t *l = job_aux_get (job, "pending-dependencies");
    if (!l) {
        if (!(l = zlistx_new ())) {
            errno = ENOMEM;
            return -1;
        }
        zlistx_set_destructor (l, dependency_destroy);
        if (job_aux_set (job, "pending-dependencies", l, zlist_free) < 0) {
            zlistx_destroy (&l);
            errno = ENOMEM;
            return -1;
        }
    }
    if (!(dp = dependency_create (add, description))
        || !zlistx_add_end (l, dp)) {
        dependency_destroy ((void **) &dp);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static int jobtap_emit_dependency_event (struct jobtap *jobtap,
                                         struct job *job,
                                         bool add,
                                         const char *description)
{
    int flags = 0;
    const char *event = add ? "dependency-add" : "dependency-remove";

    if (job->state == FLUX_JOB_STATE_NEW) {
        /*  Dependencies cannot be emitted before DEPEND state, but it
         *   is useful for plugins to generate them in job.validate or
         *   job.new. In this case, stash these dependencies as pending
         *   within the job itself. These will later be emitted as the job
         *   enters DEPEND state.
         */
        return add_pending_dependency (job, add, description);
    }
    if (job->state != FLUX_JOB_STATE_DEPEND) {
        errno = EINVAL;
        return -1;
    }
    if (!job_dependency_event_valid (job, event, description)) {
        /*  Ignore duplicate dependency-add/remove events
         */
        if (errno == EEXIST)
            return 0;
        return -1;
    }
    return event_job_post_pack (jobtap->ctx->event,
                                job,
                                event,
                                flags,
                                "{s:s}",
                                "description", description);
}

static int emit_dependency_event (flux_plugin_t *p,
                                  flux_jobid_t id,
                                  bool add,
                                  const char *description)
{
    struct job *job;
    struct jobtap *jobtap = flux_plugin_aux_get (p, "flux::jobtap");
    if (!jobtap) {
        errno = EINVAL;
        return -1;
    }
    job = jobtap->current_job;
    if (!job || id != job->id) {
        if (!(job = lookup_job (jobtap->ctx, id)))
            return -1;
    }
    return jobtap_emit_dependency_event (jobtap, job, add, description);
}

int flux_jobtap_dependency_add (flux_plugin_t *p,
                                flux_jobid_t id,
                                const char *description)
{
    return emit_dependency_event (p, id, true, description);
}

int flux_jobtap_dependency_remove (flux_plugin_t *p,
                                   flux_jobid_t id,
                                   const char *description)
{
    return emit_dependency_event (p, id, false, description);
}

static int job_emit_pending_dependencies (struct jobtap *jobtap,
                                          struct job *job)
{
    zlistx_t *l = job_aux_get (job, "pending-dependencies");
    if (l) {
        struct dependency *dp;
        FOREACH_ZLISTX (l, dp) {
            if (jobtap_emit_dependency_event (jobtap,
                                              job,
                                              dp->add,
                                              dp->description) < 0) {
                char note [128];
                (void) snprintf (note, sizeof (note),
                                 "failed to %s dependency %s",
                                 dp->add ? "add" : "remove",
                                 dp->description);
                if (event_job_post_pack (jobtap->ctx->event,
                                         job, "exception", 0,
                                         "{ s:s s:i s:i s:s }",
                                         "type", "dependency",
                                         "severity", 0,
                                         "userid", FLUX_USERID_UNKNOWN,
                                         "note", note) < 0) {
                    flux_log_error (jobtap->ctx->h,
                                    "%s: event_job_post_pack: id=%ju",
                                    __FUNCTION__, (uintmax_t) job->id);
                }
                /*  Proceed no further, job has exception and will proceed
                 *   to INACTIVE state
                 */
                break;
            }
        }
        job_aux_delete (job, l);
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

