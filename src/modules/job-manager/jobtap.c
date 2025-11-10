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
#include "src/common/libutil/basename.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/aux.h"
#include "src/common/libjob/idf58.h"
#include "ccan/str/str.h"

#include "annotate.h"
#include "prioritize.h"
#include "conf.h"
#include "event.h"
#include "raise.h"
#include "jobtap.h"
#include "jobtap-internal.h"

#define FLUX_JOBTAP_PRIORITY_UNAVAIL INT64_C(-2)

extern int priority_default_plugin_init (flux_plugin_t *p);
extern int limit_job_size_plugin_init (flux_plugin_t *p);
extern int limit_duration_plugin_init (flux_plugin_t *p);
extern int after_plugin_init (flux_plugin_t *p);
extern int singleton_plugin_init (flux_plugin_t *p);
extern int begin_time_plugin_init (flux_plugin_t *p);
extern int validate_duration_plugin_init (flux_plugin_t *p);
extern int update_duration_plugin_init (flux_plugin_t *p);
extern int history_plugin_init (flux_plugin_t *p);
extern int post_event_init (flux_plugin_t *p);

struct jobtap_builtin {
    const char *name;
    flux_plugin_init_f init;
};

struct jobtap_builtin_ex {
    char *name;
    jobtap_builtin_f init_cb;
    void *arg;
};

static struct jobtap_builtin jobtap_builtins [] = {
    { ".priority-default", priority_default_plugin_init },
    { ".limit-job-size", limit_job_size_plugin_init },
    { ".limit-duration", limit_duration_plugin_init },
    { ".dependency-after", after_plugin_init },
    { ".dependency-singleton", singleton_plugin_init },
    { ".begin-time", &begin_time_plugin_init },
    { ".validate-duration", &validate_duration_plugin_init },
    { ".update-duration", &update_duration_plugin_init },
    { ".history", &history_plugin_init },
    { ".post-event", &post_event_init },
    { 0 },
};

struct jobtap {
    struct job_manager *ctx;
    char *searchpath;
    zlistx_t *builtins_ex;
    zlistx_t *plugins;
    zhashx_t *plugins_byuuid;
    zlistx_t *jobstack;
    json_t *jobspec_update;
    bool configured;
};

struct dependency {
    bool add;
    char *description;
};

static int jobtap_job_raise (struct jobtap *jobtap,
                             struct job *job,
                             const char *type,
                             int severity,
                             const char *fmt, ...);

static int dependencies_unpack (struct jobtap * jobtap,
                                struct job * job,
                                char **errp,
                                json_t **resultp);

static int jobtap_check_dependency (struct jobtap *jobtap,
                                    flux_plugin_t *p,
                                    struct job *job,
                                    flux_plugin_arg_t *args,
                                    int index,
                                    json_t *entry,
                                    char **errp);

static struct aux_wrap *aux_wrap_get (flux_plugin_t *p,
                                      struct job *job,
                                      bool create);

/*  zlistx_t plugin destructor */
static void plugin_destroy (void **item)
{
    if (item) {
        flux_plugin_t *p = *item;
        flux_plugin_destroy (p);
        *item = NULL;
    }
}

static void jobtap_builtin_ex_destroy (struct jobtap_builtin_ex *ex)
{
    if (ex) {
        int saved_errno = errno;
        free (ex->name);
        free (ex);
        errno = saved_errno;
    }
}

/*  zlistx_t jobtap_builtin_ex destructor */
static void builtin_ex_destructor (void **item)
{
    if (item) {
        struct jobtap_builtin_ex *ex = *item;
        jobtap_builtin_ex_destroy (ex);
        *item = NULL;
    }
}

struct jobtap_builtin_ex * jobtap_builtin_ex_create (const char *name,
                                                     jobtap_builtin_f init_cb,
                                                     void *arg)
{
    struct jobtap_builtin_ex *ex = calloc (1, sizeof (*ex));
    if (!ex || !(ex->name = strdup (name)))
        goto error;
    ex->init_cb = init_cb;
    ex->arg = arg;
    return ex;
error:
    jobtap_builtin_ex_destroy (ex);
    return NULL;
}

static const char *jobtap_plugin_name (flux_plugin_t *p)
{
    const char *name;
    if (!p)
        return "none";
    if ((name = flux_plugin_aux_get (p, "jobtap::basename"))
        || (name = flux_plugin_get_name (p)))
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
                              "{s:O s:I s:I s:i s:i s:I s:f}",
                              "jobspec", job->jobspec_redacted,
                              "id", job->id,
                              "userid", (json_int_t) job->userid,
                              "urgency", job->urgency,
                              "state", job->state,
                              "priority", job->priority,
                              "t_submit", job->t_submit) < 0)
        goto error;
    if (job->R_redacted) {
        if (flux_plugin_arg_pack (args,
                                  FLUX_PLUGIN_ARG_IN,
                                  "{s:O}",
                                  "R", job->R_redacted) < 0)
            goto error;
    }
    if (job->end_event)
        if (flux_plugin_arg_pack (args,
                                  FLUX_PLUGIN_ARG_IN,
                                  "{s:O}",
                                  "end_event", job->end_event) < 0)
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
                                  FLUX_PLUGIN_ARG_IN,
                                  fmt, ap) < 0)
        goto error;
    return args;
error:
    flux_plugin_arg_destroy (args);
    return NULL;
}


static int plugin_check_dependencies (struct jobtap *jobtap,
                                      flux_plugin_t *p,
                                      struct job *job,
                                      flux_plugin_arg_t *args)
{
    json_t *dependencies = NULL;
    json_t *entry = NULL;
    size_t index;
    char *error;

    if (dependencies_unpack (jobtap, job, &error, &dependencies) < 0) {
        flux_log (jobtap->ctx->h,
                  LOG_ERR,
                  "id=%s: plugin_register_dependencies: %s",
                  idf58 (job->id),
                  error);
        free (error);
        return -1;
    }

    if (dependencies == NULL)
        return 0;

    json_array_foreach (dependencies, index, entry) {
        char *error;
        if (jobtap_check_dependency (jobtap,
                                     p,
                                     job,
                                     args,
                                     index,
                                     entry,
                                     &error) < 0) {
            flux_log (jobtap->ctx->h,
                      LOG_ERR,
                      "plugin_check_dependencies: %s", error);
        }
    }
    return 0;
}

static struct job * current_job (struct jobtap *jobtap)
{
    return zlistx_head (jobtap->jobstack);
}

static int current_job_push (struct jobtap *jobtap, struct job *job)
{
    if (!zlistx_add_start (jobtap->jobstack, job)) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static int current_job_pop (struct jobtap *jobtap)
{
    return zlistx_delete (jobtap->jobstack, NULL);
}

static int job_id_cmp (const void *a, const void *b)
{
    const struct job *job1 = a;
    const struct job *job2 = b;
    if (job1->id == job2->id)
        return 0;
    if (job1->id < job2->id)
        return -1;
    return 1;
}

/* Sort jobs by state, then submit time (via jobid)
 */
static int job_state_cmp (const void *a, const void *b)
{
    const struct job *job1 = a;
    const struct job *job2 = b;
    if (job1->state == job2->state)
        return job_id_cmp (a, b);
    return job1->state < job2->state ? -1 : 1;
}

/* Sort jobs by reverse state, then submit time (via jobid)
 */
static int job_state_reverse_cmp (const void *a, const void *b)
{
    const struct job *job1 = a;
    const struct job *job2 = b;
    if (job1->state == job2->state)
        return job_id_cmp (a, b);
    return job1->state > job2->state ? -1 : 1;
}

static void job_list_sort (zlistx_t *jobs, const char *mode)
{
    if (streq (mode, "state"))
        zlistx_set_comparator (jobs, job_state_cmp);
    else if (streq (mode, "-state"))
        zlistx_set_comparator (jobs, job_state_reverse_cmp);
    else
        return;
    zlistx_sort (jobs);
}

/* Return true if `mode` is a valid job sort order
 */
static inline bool sort_mode_valid (const char *mode)
{
    return (streq (mode, "none")
            || streq (mode, "state")
            || streq (mode, "-state"));
}

int flux_jobtap_set_load_sort_order (flux_plugin_t *p, const char *mode)
{
    char *sort_mode = NULL;
    struct jobtap *jobtap = flux_plugin_aux_get (p, "flux::jobtap");

    if (!jobtap || !mode || !sort_mode_valid (mode)) {
        errno = EINVAL;
        return -1;
    }

    /* avoid unnecessary copy by ignoring "none" (same as unset)
     */
    if (streq (mode, "none"))
        return 0;

    if (!(sort_mode = strdup (mode))
        || flux_plugin_aux_set (p,
                                "jobtap::sort_mode",
                                sort_mode,
                                free) < 0) {
        ERRNO_SAFE_WRAP (free, sort_mode);
        return -1;
    }
    return 0;
}

static flux_plugin_t * jobtap_load_plugin (struct jobtap *jobtap,
                                           const char *path,
                                           json_t *conf,
                                           flux_error_t *errp)
{
    struct job_manager *ctx = jobtap->ctx;
    const char *sort_mode = NULL;
    flux_plugin_t *p = NULL;
    flux_plugin_arg_t *args;
    zlistx_t *jobs;
    struct job *job;

    if (!(p = jobtap_load (jobtap, path, conf, errp)))
        goto error;

    /*  Make plugin aware of all active jobs.
     */
    if (!(jobs = zhashx_values (ctx->active_jobs))) {
        errprintf (errp, "zhashx_values() failed");
        goto error;
    }
    /*  Sort jobs if requested
     */
    if ((sort_mode = flux_plugin_aux_get (p, "jobtap::sort_mode")))
        job_list_sort (jobs, sort_mode);

    job = zlistx_first (jobs);
    while (job) {
        if (current_job_push (jobtap, job) < 0) {
            errprintf (errp, "Out of memory adding to jobtap jobstack");
            goto error;
        }
        if (!(args = jobtap_args_create (jobtap, job))) {
            errprintf (errp, "Failed to create args for job");
            goto error;
        }

        /*  Notify this plugin of all jobs via `job.create` and `job.new`
         *   callbacks.
         */
        (void) flux_plugin_call (p, "job.create", args);
        (void) flux_plugin_call (p, "job.new", args);

        /*  If job is in DEPEND state then there may be pending dependencies.
         *  Notify plugin of the DEPEND state assuming it needs to create
         *   some state in order to resolve the dependency.
         */
        if (job->state == FLUX_JOB_STATE_DEPEND) {
            if (plugin_check_dependencies (jobtap, p, job, args) < 0)
                errprintf (errp,
                           "failed to check dependencies for job %s",
                           idf58 (job->id));
            (void) flux_plugin_call (p, "job.state.depend", args);
        }

        flux_plugin_arg_destroy (args);
        if (current_job_pop (jobtap)) {
            errprintf (errp, "Error popping current job off jobtap stack");
            goto error;
        }
        job = zlistx_next (jobs);
    }
    zlistx_destroy (&jobs);

    /* Now schedule reprioritize of all jobs
     */
    if (reprioritize_all (ctx) < 0) {
        errprintf (errp,
                   "%s loaded but unable to reprioritize jobs",
                   jobtap_plugin_name (p));
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

static void jobtap_finalize (struct jobtap *jobtap, flux_plugin_t *p)
{
    zlistx_t *jobs;

    if ((jobs = zhashx_values (jobtap->ctx->active_jobs))) {
        struct job *job;

        job = zlistx_first (jobs);
        while (job) {
            struct aux_wrap *wrap;

            if ((wrap = aux_wrap_get (p, job, false)))
                job_aux_delete (job, wrap);

            job = zlistx_next (jobs);
        }
        zlistx_destroy (&jobs);
    }
}

static int jobtap_remove (struct jobtap *jobtap,
                          const char *arg,
                          flux_error_t *errp)
{
    int count = 0;
    bool isglob = isa_glob (arg);
    bool all = streq (arg, "all");

    flux_plugin_t *p = zlistx_first (jobtap->plugins);
    while (p) {
        const char *name = jobtap_plugin_name (p);
        if ((all && name[0] != '.')
            || (isglob && fnmatch (arg, name, FNM_PERIOD) == 0)
            || streq (arg, name)) {
            jobtap_finalize (jobtap, p);
            zhashx_delete (jobtap->plugins_byuuid, flux_plugin_get_uuid (p));
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
                              flux_error_t *errp)
{
    json_error_t json_err;
    flux_error_t jobtap_err;
    const char *load = NULL;
    const char *remove = NULL;
    json_t *conf = NULL;

    if (json_unpack_ex (entry,
                        &json_err,
                        0,
                        "{s?s s?o s?s}",
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
    if (load && !jobtap_load_plugin (jobtap, load, conf, &jobtap_err)) {
        return errprintf (errp,
                          "[job-manager.plugins][%d]: load: %s",
                          index,
                          jobtap_err.text);
    }
    return 0;
}

static int jobtap_call_conf_update (flux_plugin_t *p,
                                    const flux_conf_t *conf,
                                    flux_error_t *errp)
{
    const char *name = flux_plugin_get_name (p);
    flux_plugin_arg_t *args;
    json_t *o;

    if (flux_conf_unpack (conf, errp, "o", &o) < 0)
        return -1;
    if (!(args = flux_plugin_arg_create ())
        || flux_plugin_arg_pack (args,
                                 FLUX_PLUGIN_ARG_IN,
                                 "{s:O}",
                                 "conf", o) < 0) {
        errprintf (errp, "error preparing args for %s jobtap plugin", name);
        goto error;
    }
    if (flux_plugin_call (p, "conf.update", args) < 0) {
        const char *errmsg;
        if (flux_plugin_arg_unpack (args,
                                    FLUX_PLUGIN_ARG_OUT,
                                    "{s:s}",
                                    "errmsg", &errmsg) < 0)
            errprintf (errp, "config rejected by %s jobtap plugin", name);
        else
            errprintf (errp, "%s", errmsg);
        errno = EINVAL;
        goto error;
    }
    flux_plugin_arg_destroy (args);
    return 0;
error:
    flux_plugin_arg_destroy (args);
    return -1;
}

static int jobtap_stack_call_conf_update (struct jobtap *jobtap,
                                          const flux_conf_t *conf,
                                          flux_error_t *errp)
{
    flux_plugin_t *p;

    p = zlistx_first (jobtap->plugins);
    while (p) {
        if (jobtap_call_conf_update (p, conf, errp) < 0)
            return -1;
        p = zlistx_next (jobtap->plugins);
    }
    return 0;
}

static int jobtap_parse_config (const flux_conf_t *conf,
                                flux_error_t *errp,
                                void *arg)
{
    struct jobtap *jobtap = arg;
    json_t *plugins = NULL;
    flux_error_t error;
    json_t *entry;
    int i;

    if (!conf)
        return errprintf (errp, "conf object can't be NULL");

    /* Changes to [job-manager.plugins] are currently ignored.
     */
    if (!jobtap->configured) {
        if (flux_conf_unpack (conf,
                              &error,
                              "{s?{s?o}}",
                              "job-manager",
                                "plugins", &plugins) < 0) {
            return errprintf (errp,
                              "[job-manager.plugins]: unpack error: %s",
                              error.text);
        }
        if (plugins) {
            if (!json_is_array (plugins)) {
                return errprintf (errp,
                             "[job-manager.plugins] config must be an array");
            }
            json_array_foreach (plugins, i, entry) {
                if (jobtap_conf_entry (jobtap, i, entry, errp) < 0)
                    return -1;
            }
        }
        jobtap->configured = true;
    }

    /* Process plugins that want 'conf.update' notifications.
     * In this case the 'conf' object is the entire instance config
     * rather than [job-manager.plugins.<name>.conf].
     */
    if (jobtap_stack_call_conf_update (jobtap, conf, errp) < 0)
        return -1;

    return 1; // indicates to conf.c that callback wants updates
}

static int plugin_byname (const void *item1, const void *item2)
{
    const char *name1 = jobtap_plugin_name ((flux_plugin_t *) item1);
    const char *name2 = item2;
    if (!name1 || !name2)
        return -1;
    return strcmp (name1, name2);
}

static int load_builtins (struct jobtap *jobtap)
{
    struct jobtap_builtin *builtin = jobtap_builtins;
    flux_error_t error;

    while (builtin && builtin->name) {
        /*  Yes, this will require re-scanning the builtin plugin list
         *   in order to lookup the plugin init function by name for
         *   each loaded plugin. However, this keeps code duplication
         *   down since jobtap_load() does a lot of work. Plus, this
         *   is only called once at job-manager startup.
         *
         *  If the size of the builtins list gets large this should be
         *   revisited.
         */
        if (!jobtap_load (jobtap, builtin->name, NULL, &error)) {
            flux_log (jobtap->ctx->h,
                      LOG_ERR,
                      "jobtap: %s: %s",
                      builtin->name,
                      error.text);
            return -1;
        }
        builtin++;
    }
    return 0;
}

struct jobtap *jobtap_create (struct job_manager *ctx)
{
    const char *path;
    flux_error_t error;
    struct jobtap *jobtap = calloc (1, sizeof (*jobtap));
    if (!jobtap)
        return NULL;
    jobtap->ctx = ctx;
    if ((path = flux_conf_builtin_get ("jobtap_pluginpath", FLUX_CONF_AUTO))
        && !(jobtap->searchpath = strdup (path)))
        goto error;
    if (!(jobtap->plugins = zlistx_new ())
        || !(jobtap->plugins_byuuid = zhashx_new ())
        || !(jobtap->jobstack = zlistx_new ())
        || !(jobtap->builtins_ex = zlistx_new ())) {
        errno = ENOMEM;
        goto error;
    }
    zlistx_set_destructor (jobtap->plugins, plugin_destroy);
    zlistx_set_comparator (jobtap->plugins, plugin_byname);
    zhashx_set_key_duplicator (jobtap->plugins_byuuid, NULL);
    zhashx_set_key_destructor (jobtap->plugins_byuuid, NULL);
    zlistx_set_destructor (jobtap->jobstack, job_destructor);
    zlistx_set_duplicator (jobtap->jobstack, job_duplicator);
    zlistx_set_destructor (jobtap->builtins_ex, builtin_ex_destructor);


    if (load_builtins (jobtap) < 0) {
        flux_log (ctx->h, LOG_ERR, "jobtap: failed to init builtins");
        goto error;
    }

    if (conf_register_callback (ctx->conf,
                                &error,
                                jobtap_parse_config,
                                jobtap) < 0) {
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
        conf_unregister_callback (jobtap->ctx->conf, jobtap_parse_config);
        zlistx_destroy (&jobtap->plugins);
        zhashx_destroy (&jobtap->plugins_byuuid);
        zlistx_destroy (&jobtap->jobstack);
        zlistx_destroy (&jobtap->builtins_ex);
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

static int jobtap_post_jobspec_updates (struct jobtap *jobtap,
                                        struct job *job)
{
    int rc;
    json_t *update = jobtap->jobspec_update;

    if (!update)
        return 0;
    /*
     * Note: Ensure this function is reentrant by nullifying
     * jobtap->jobspec_update before posting the jobspec-update event.
     * The jobspec-update event may trigger other jobtap callbacks, most
     * notably via a call to "job.update".
     */
    jobtap->jobspec_update = NULL;
    rc = event_job_post_pack (jobtap->ctx->event,
                              job,
                              "jobspec-update",
                              0,
                              "O",
                              update);
    ERRNO_SAFE_WRAP (json_decref, update);
    return rc;
}

static int jobtap_stack_call (struct jobtap *jobtap,
                              zlistx_t *plugins,
                              struct job *job,
                              const char *topic,
                              flux_plugin_arg_t *args)
{
    int retcode = 0;
    flux_plugin_t *p = NULL;

    /* Duplicate list to make jobtap_stack_call reentrant */
    zlistx_t *l = zlistx_dup (plugins);
    if (!l)
        return -1;
    zlistx_set_destructor (l, NULL);

    if (current_job_push (jobtap, job) < 0)
        return -1;
    p = zlistx_first (l);
    while (p) {
        int rc = flux_plugin_call (p, topic, args);
        if (rc < 0)  {
            flux_log (jobtap->ctx->h,
                      LOG_DEBUG,
                      "jobtap: %s: %s: rc=%d",
                      jobtap_plugin_name (p),
                      topic,
                      rc);
            retcode = -1;
            break;
        }
        /*  Post any pending jobspec updates now. This is done after
         *  the callback returns to avoid rewriting jobspec during a
         *  plugin callback that modifies it.
         */
        if (jobtap_post_jobspec_updates (jobtap, job) < 0) {
            flux_log_error (jobtap->ctx->h,
                            "jobtap: %s: %s: failed to apply jobspec updates",
                            jobtap_plugin_name (p),
                            topic);
            retcode = -1;
            break;
        }
        retcode += rc;
        p = zlistx_next (l);
    }
    zlistx_destroy (&l);
    if (current_job_pop (jobtap) < 0)
        return -1;
    return retcode;
}

int jobtap_get_priority (struct jobtap *jobtap,
                         struct job *job,
                         int64_t *pprio)
{
    int rc = -1;
    flux_plugin_arg_t *args;
    int64_t priority = FLUX_JOBTAP_PRIORITY_UNAVAIL;

    if (!jobtap || !job || !pprio) {
        errno = EINVAL;
        return -1;
    }

    if (!(args = jobtap_args_create (jobtap, job)))
        return -1;

    rc = jobtap_stack_call (jobtap,
                            jobtap->plugins,
                            job,
                            "job.priority.get",
                            args);

    if (rc >= 1) {
        /*
         *  A priority.get callback was run. Try to unpack a new priority
         */
        if (flux_plugin_arg_unpack (args,
                                    FLUX_PLUGIN_ARG_OUT,
                                    "{s?I}",
                                    "priority", &priority) < 0) {
            flux_log (jobtap->ctx->h,
                      LOG_ERR,
                      "jobtap: job.priority.get: arg_unpack: %s",
                      flux_plugin_arg_strerror (args));
            /*  Note failure, but keep current priority */
            priority = job->priority;
            rc = -1;
        }
        if (priority == FLUX_JOBTAP_PRIORITY_UNAVAIL) {
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
                flux_log (jobtap->ctx->h,
                          LOG_ERR,
                          "jobtap: %s: BUG: plugin didn't return priority",
                          idf58 (job->id));
        }
        /*
         *   O/w, plugin provided a new priority.
         */
    }
    else if (rc < 0) {
        /*
         *  priority.get callback was run and failed. Log the error
         *   and return the current priority.
         */
        flux_log (jobtap->ctx->h,
                  LOG_ERR,
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
    int saved_errno = errno;
    va_start (ap, fmt);
    if (vasprintf (errp, fmt, ap) < 0)
        flux_log_error (jobtap->ctx->h,
                        "id=%s: failed to create error string: fmt=%s",
                        idf58 (job->id), fmt);
    va_end (ap);
    errno = saved_errno;
}

/* Common function for job.create and job.validate.
 * Both can reject a job with textual error for the submit RPC.
 */
static int jobtap_call_early (struct jobtap *jobtap,
                              struct job *job,
                              const char *topic,
                              char **errp)
{
    int rc;
    flux_plugin_arg_t *args;
    const char *errmsg = NULL;

    if (jobtap_topic_match_count (jobtap, topic) == 0)
        return 0;
    if (!(args = jobtap_args_create (jobtap, job)))
        return -1;

    rc = jobtap_stack_call (jobtap,
                            jobtap->plugins,
                            job,
                            topic,
                            args);

    if (rc < 0) {
        /*
         *  Plugin callback failed, check for errmsg for this job
         *   If plugin did not provide an error message, then construct
         *   a generic error "rejected by plugin".
         */
        if (flux_plugin_arg_unpack (args,
                                    FLUX_PLUGIN_ARG_OUT,
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

int jobtap_validate (struct jobtap *jobtap, struct job *job, char **errp)
{
    return jobtap_call_early (jobtap, job, "job.validate", errp);
}

int jobtap_call_create (struct jobtap *jobtap, struct job *job, char **errp)
{
    return jobtap_call_early (jobtap, job, "job.create", errp);
}

static int make_dependency_topic (struct jobtap *jobtap,
                                  struct job *job,
                                  int index,
                                  json_t *entry,
                                  const char **schemep,
                                  char *topic,
                                  int topiclen,
                                  char **errp)
{
    *schemep = NULL;
    if (json_unpack (entry, "{s:s}", "scheme", schemep) < 0
        || *schemep == NULL) {
        error_asprintf (jobtap,
                        job,
                        errp,
                        "dependency[%d] missing string scheme",
                        index);
        return -1;
    }

    if (snprintf (topic,
                  topiclen,
                  "job.dependency.%s",
                  *schemep) > topiclen) {
        error_asprintf (jobtap, job, errp,
                        "rejecting absurdly long dependency scheme: %s",
                        *schemep);
        return -1;
    }

    return 0;
}

static int jobtap_check_dependency (struct jobtap *jobtap,
                                    flux_plugin_t *p,
                                    struct job *job,
                                    flux_plugin_arg_t *args,
                                    int index,
                                    json_t *entry,
                                    char **errp)
{
    int rc = -1;
    char topic [128];
    const char *scheme = NULL;

    if (make_dependency_topic (jobtap,
                               job,
                               index,
                               entry,
                               &scheme,
                               topic,
                               sizeof (topic),
                               errp) < 0)
        return -1;

    /*  If we're only calling this topic for a single plugin, and there
     *   is no matching handler, return without error immediately
     */
    if (p && !flux_plugin_match_handler (p, topic))
        return 0;

    if (flux_plugin_arg_pack (args,
                              FLUX_PLUGIN_ARG_IN,
                              "{s:O}",
                              "dependency", entry) < 0
        || flux_plugin_arg_set (args, FLUX_PLUGIN_ARG_OUT, "{}") < 0) {
        flux_log_error (jobtap->ctx->h,
                        "jobtap_check_dependency: failed to prepare args");
        return -1;
    }

    if (p)
        rc = flux_plugin_call (p, topic, args);
    else
        rc = jobtap_stack_call (jobtap, jobtap->plugins, job, topic, args);

    if (rc == 0) {
        /*  No handler for job.dependency.<scheme>. return an error.
         */
        error_asprintf (jobtap,
                        job,
                        errp,
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
        if (flux_plugin_arg_unpack (args,
                                    FLUX_PLUGIN_ARG_OUT,
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

    if (json_unpack_ex (job->jobspec_redacted,
                        &error,
                        0,
                        "{s:{s?{s?o}}}",
                        "attributes",
                          "system",
                            "dependencies", &dependencies) < 0) {
        error_asprintf (jobtap,
                        job,
                        errp,
                        "unable to unpack dependencies: %s",
                        error.text);
        return -1;
    }

    if (!dependencies)
        return 0;

    if (!json_is_array (dependencies)) {
        error_asprintf (jobtap,
                        job,
                        errp,
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
                               bool raise_exception,
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
        error_asprintf (jobtap,
                        job,
                        errp,
                        "jobtap_check_dependencies: failed to create args");
        return -1;
    }

    json_array_foreach (dependencies, index, entry) {
        rc = jobtap_check_dependency (jobtap,
                                      NULL,
                                      job,
                                      args,
                                      index,
                                      entry,
                                      errp);
        if (rc < 0) {
            if (!raise_exception)
                goto out;
            if (jobtap_job_raise (jobtap,
                                  job,
                                  "dependency",
                                  4, /* LOG_WARNING */
                                  "%s (job may be stuck in DEPEND state)",
                                  *errp) < 0)
                flux_log_error (jobtap->ctx->h,
                                "id=%s: failed to raise dependency exception",
                                idf58 (job->id));
            free (*errp);
            *errp = NULL;
        }
    }
    rc = 0;
out:
    flux_plugin_arg_destroy (args);
    return rc;
}

int jobtap_notify_subscribers (struct jobtap *jobtap,
                               struct job *job,
                               const char *name,
                               const char *fmt,
                               ...)
{
    flux_plugin_arg_t *args;
    char topic [64];
    int topiclen = 64;
    va_list ap;
    int rc;

    if (!job->subscribers)
        return 0;

    if (snprintf (topic, topiclen, "job.event.%s", name) >= topiclen) {
        flux_log (jobtap->ctx->h,
                  LOG_ERR,
                  "jobtap: %s: %s: event topic name too long",
                  name,
                  idf58 (job->id));
        return -1;
    }

    va_start (ap, fmt);
    args = jobtap_args_vcreate (jobtap, job, fmt, ap);
    va_end (ap);
    if (!args) {
        flux_log (jobtap->ctx->h,
                  LOG_ERR,
                  "jobtap: %s: %s: failed to create plugin args",
                  topic,
                  idf58 (job->id));
        return -1;
    }

    rc = jobtap_stack_call (jobtap, job->subscribers, job, topic, args);
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
    json_t *R = NULL;
    flux_plugin_arg_t *args;
    int64_t priority = FLUX_JOBTAP_PRIORITY_UNAVAIL;
    va_list ap;

    if (jobtap_topic_match_count (jobtap, topic) == 0)
        return 0;

    va_start (ap, fmt);
    if (!(args = jobtap_args_vcreate (jobtap, job, fmt, ap))) {
        flux_log (jobtap->ctx->h,
                  LOG_ERR,
                  "jobtap: %s: %s: failed to create plugin args",
                  topic,
                  idf58 (job->id));
    }
    va_end (ap);

    if (!args)
        return -1;

    rc = jobtap_stack_call (jobtap, jobtap->plugins, job, topic, args);
    if (rc < 0) {
        flux_log (jobtap->ctx->h,
                  LOG_ERR,
                  "jobtap: %s: callback returned error",
                  topic);
    }
    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_OUT,
                                "{s?I s?o s?o}",
                                "priority", &priority,
                                "annotations", &note,
                                "R", &R) < 0) {
        if (jobtap_job_raise (jobtap,
                              job,
                              topic, 4,
                              "arg_unpack: %s%s",
                              flux_plugin_arg_strerror (args),
                              job->state == FLUX_JOB_STATE_PRIORITY ?
                              " (job may be stuck in PRIORITY state)" : "") < 0)
            flux_log (jobtap->ctx->h,
                      LOG_ERR,
                      "%s: jobtap_job_raise: %s",
                       topic,
                       strerror (errno));
        rc = -1;
    }
    if (R != NULL) {
        if (!streq (topic, "job.state.sched")) {
            flux_log (jobtap->ctx->h,
                      LOG_ERR,
                      "jobtap: %s: %s: R may only be set in SCHED state",
                      topic,
                      idf58 (job->id));
            rc = -1;
        }
        else if (job->R_redacted) {
            flux_log (jobtap->ctx->h,
                      LOG_ERR,
                      "jobtap: %s: %s: R is already set",
                      topic,
                      idf58 (job->id));
            rc = -1;
        }
        else
            job->R_redacted = json_incref (R);
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
        int ret;
        if (streq (topic, "job.new"))
            ret = annotations_update (job, ".", note);
        else
            ret = annotations_update_and_publish (jobtap->ctx, job, note);
        if (ret < 0)
            flux_log_error (jobtap->ctx->h,
                            "jobtap: %s: %s: annotations_update",
                            topic,
                            idf58 (job->id));
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
        if (streq (name, builtin->name)) {
            if (flux_plugin_set_name (p, builtin->name) < 0)
                return -1;
            return (*builtin->init) (p);
        }
        builtin++;
    }

    errno = ENOENT;
    return -1;
}

static int jobtap_load_builtin_ex (struct jobtap *jobtap,
                                   flux_plugin_t *p,
                                   const char *name)
{
    struct jobtap_builtin_ex *ex = zlistx_first (jobtap->builtins_ex);
    while (ex) {
        if (streq (name, ex->name)) {
            if (flux_plugin_set_name (p, ex->name) < 0)
                return -1;
            return (*ex->init_cb) (p, ex->arg);
        }
        ex = zlistx_next (jobtap->builtins_ex);
    }
    errno = ENOENT;
    return -1;
}

int jobtap_register_builtin (struct jobtap *jobtap,
                             const char *name,
                             jobtap_builtin_f init_cb,
                             void *arg)
{
    struct jobtap_builtin_ex *ex;

    if (!jobtap || !name || !init_cb || !strstarts (name, ".")) {
        errno = EINVAL;
        return -1;
    }
    if (!(ex = jobtap_builtin_ex_create (name, init_cb, arg)))
        return -1;
    if (!(zlistx_add_end (jobtap->builtins_ex, ex))) {
        jobtap_builtin_ex_destroy (ex);
        errno = ENOMEM;
    }
    return 0;
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

static int plugin_set_name (flux_plugin_t *p,
                            const char *basename)
{
    int rc = -1;
    char *q;
    char *copy = NULL;
    const char *name = flux_plugin_get_name (p);

    /*  It is ok to have a custom name, but that name may
     *   not contain '/' or '.'
     */
    if (name && !strchr (name, '/') && !strchr (name, '.'))
        return 0;
    if (!(copy = strdup (basename)))
        return -1;
    if ((q = strchr (copy, '.')))
        *q = '\0';
    rc = flux_plugin_set_name (p, copy);
    ERRNO_SAFE_WRAP (free, copy);
    return rc;
}

static int plugin_try_load (struct jobtap *jobtap,
                            flux_plugin_t *p,
                            const char *fullpath,
                            flux_error_t *errp)
{
    char *name = NULL;

    if (flux_plugin_load_dso (p, fullpath) < 0)
        return errprintf (errp,
                          "%s",
                          flux_plugin_strerror (p));
    if (!(name = strdup (basename_simple (fullpath)))
        || flux_plugin_aux_set (p, "jobtap::basename", name, free) < 0) {
        ERRNO_SAFE_WRAP (free, name);
        return errprintf (errp,
                          "%s: failed to create plugin basename",
                          fullpath);
    }
    if (plugin_set_name (p, name) < 0)
        return errprintf (errp,
                          "%s: unable to set a plugin name",
                           fullpath);
    if (zlistx_find (jobtap->plugins, (void *) jobtap_plugin_name (p))) {
        errno = EEXIST;
        return errprintf (errp,
                          "%s already loaded",
                          jobtap_plugin_name (p));
    }
    return 0;
}

int jobtap_plugin_load_first (struct jobtap *jobtap,
                              flux_plugin_t *p,
                              const char *path,
                              flux_error_t *errp)
{
    bool found = false;
    zlistx_t *l;
    char *fullpath;

    if (no_searchpath (jobtap->searchpath, path))
        return plugin_try_load (jobtap, p, path, errp);

    if (!(l = path_list (jobtap->searchpath, path)))
        return -1;

    fullpath = zlistx_first (l);
    while (fullpath) {
        int rc = plugin_try_load (jobtap, p, fullpath, errp);
        if (rc < 0 && errno != ENOENT) {
            ERRNO_SAFE_WRAP (zlistx_destroy , &l);
            return -1;
        }
        if (rc == 0) {
            found = true;
            break;
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

static bool is_builtin (const char *path)
{
    /*  A builtin plugin starts with '.' and does not contain a slash
     */
    return (path[0] == '.' && !strchr (path, '/'));
}

flux_plugin_t * jobtap_load (struct jobtap *jobtap,
                             const char *path,
                             json_t *conf,
                             flux_error_t *errp)
{
    flux_plugin_t *p = NULL;
    char *conf_str = NULL;

    err_init (errp);

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
    if (is_builtin (path)) {
        if (jobtap_load_builtin (p, path) < 0
            && jobtap_load_builtin_ex (jobtap, p, path) < 0)
            goto error;
    }
    else {
        flux_plugin_set_flags (p, FLUX_PLUGIN_RTLD_NOW);
        if (jobtap_plugin_load_first (jobtap, p, path, errp) < 0)
            goto error;
    }
    /* Call conf.update here for two reasons
     * - fail the plugin load if config is invalid
     * - make sure plugin has config before job.* callbacks begin
     */
    if (jobtap_call_conf_update (p, flux_get_conf (jobtap->ctx->h), errp) < 0)
        goto error;

    char *uuid = (char *)flux_plugin_get_uuid (p);
    if (zhashx_insert (jobtap->plugins_byuuid, uuid, p) < 0) {
        errprintf (errp, "Error adding plugin to list");
        errno = EEXIST;
        goto error;
    }
    if (!zlistx_add_end (jobtap->plugins, p)) {
        zhashx_delete (jobtap->plugins_byuuid, uuid);
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
    flux_error_t error;
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
    flux_error_t error;
    flux_plugin_t *p = NULL;

    if (!(p = jobtap_load_plugin (ctx->jobtap, path, conf, &error))) {
        if (flux_respond_error (ctx->h,
                                msg,
                                errno ? errno : EINVAL,
                                error.text[0] ? error.text : NULL) < 0)
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
    else if (flux_respond_pack (h,
                                msg,
                                "{s:o}",
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

static int jobtap_query_plugin (flux_plugin_t *p,
                                char **json_str,
                                flux_error_t *errp)
{
    int rc = -1;
    flux_plugin_arg_t *args;
    const char *path = flux_plugin_get_path (p);
    const char *name = jobtap_plugin_name (p);

    if (path == NULL)
        path = "builtin";

    if (!(args = flux_plugin_arg_create ()))
        return errprintf (errp,
                          "flux_plugin_arg_create: %s",
                          strerror (errno));

    if (flux_plugin_arg_pack (args,
                              FLUX_PLUGIN_ARG_OUT,
                             "{s:s s:s}",
                             "name", name,
                             "path", path) < 0) {
        errprintf (errp, "%s", flux_plugin_arg_strerror (args));
        goto out;
    }

    if (flux_plugin_call (p, "plugin.query", args) < 0) {
        errprintf (errp, "plugin.query failed");
        goto out;
    }

    if (flux_plugin_arg_get (args, FLUX_PLUGIN_ARG_OUT, json_str) < 0
        && errno != ENOENT) {
        errprintf (errp,
                   "failed to get plugin.query out args: %s",
                   strerror (errno));
        goto out;
    }
    rc = 0;
out:
    flux_plugin_arg_destroy (args);
    return rc;
}

void jobtap_query_handler (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct job_manager *ctx = arg;
    const char *name = NULL;
    char *result = NULL;
    flux_plugin_t *p;
    flux_error_t error;
    bool found = false;

    if (flux_request_unpack (msg, NULL, "{s:s}", "name", &name) < 0) {
        errprintf (&error, "Protocol error");
        goto error;
    }

    p = zlistx_first (ctx->jobtap->plugins);
    while (p) {
        if (streq (name, jobtap_plugin_name (p))) {
            found = true;
            if (jobtap_query_plugin (p, &result, &error) < 0)
                goto error;
            break;
        }
        p = zlistx_next (ctx->jobtap->plugins);
    }
    if (!found) {
        errprintf (&error, "%s: plugin not found", name);
        goto error;
    }
    if (flux_respond (h, msg, result) < 0)
        flux_log_error (h, "jobtap_query_handler: flux_respond");
    free (result);
    return;
error:
    if (flux_respond_error (h, msg, 0, error.text) < 0)
        flux_log_error (h, "jobtap_query_handler: flux_respond_error");
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
    /*  N.B. use plugin provided or sanitized name (trailing .so removed)
     *   in topic string. This name is stored as the main plugin name.
     */
    const char *name = flux_plugin_get_name (p);

    /*
     *  Detect improperly initialized plugin name before continuing:
     */
    if (name == NULL || strchr (name, '/')) {
        errno = EINVAL;
        return -1;
    }
    if (*name == '.') // skip conventional "." prefix used in hidden plugins
        name++;
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

int flux_jobtap_service_register_ex (flux_plugin_t *p,
                                     const char *method,
                                     uint32_t rolemask,
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
    flux_msg_handler_allow_rolemask (mh, rolemask);
    flux_msg_handler_start (mh);
    flux_log (h, LOG_DEBUG, "jobtap plugin %s registered method %s",
              jobtap_plugin_name (p),
              topic);
    return 0;
}

int flux_jobtap_service_register (flux_plugin_t *p,
                                  const char *method,
                                  flux_msg_handler_f cb,
                                  void *arg)
{
    return flux_jobtap_service_register_ex (p, method, 0, cb, arg);
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
                                 FLUX_PLUGIN_ARG_OUT,
                                 "{s:I}",
                                 "priority", FLUX_JOBTAP_PRIORITY_UNAVAIL);
}

static void jobtap_verror (flux_plugin_t *p,
                           flux_plugin_arg_t *args,
                           const char *fmt,
                           va_list ap)
{
    flux_error_t error;

    verrprintf (&error, fmt, ap);

    if (error.text[0] != '\0') {
        if (flux_plugin_arg_pack (args,
                                  FLUX_PLUGIN_ARG_OUT,
                                  "{s:s}",
                                  "errmsg", error.text) < 0) {
            flux_log_error (flux_jobtap_get_flux (p),
                            "flux_jobtap_reject_job: failed to pack error");
        }
    }
}

int flux_jobtap_error (flux_plugin_t *p,
                       flux_plugin_arg_t *args,
                       const char *fmt,
                       ...)
{
    va_list ap;
    va_start (ap, fmt);
    jobtap_verror (p, args, fmt, ap);
    va_end (ap);
    return -1;
}


int flux_jobtap_reject_job (flux_plugin_t *p,
                            flux_plugin_arg_t *args,
                            const char *fmt,
                            ...)
{
    if (fmt) {
        va_list ap;
        va_start (ap, fmt);
        jobtap_verror (p, args, fmt, ap);
        va_end (ap);
    }
    else {
        flux_jobtap_error (p,
                           args,
                           "rejected by job-manager plugin '%s'",
                           jobtap_plugin_name (p));
    }
    return -1;
}

static struct job *lookup_active_job (struct job_manager *ctx,
                                      flux_jobid_t id)
{
    struct job *job = zhashx_lookup (ctx->active_jobs, &id);
    if (!job)
        errno = ENOENT;
    return job;
}

static struct job *lookup_job (struct job_manager *ctx, flux_jobid_t id)
{
    struct job *job;
    if (!(job = lookup_active_job (ctx, id))
        && !(job = zhashx_lookup (ctx->inactive_jobs, &id)))
        errno = ENOENT;
    return job;
}

static int jobtap_emit_dependency_event (struct jobtap *jobtap,
                                         struct job *job,
                                         bool add,
                                         const char *description)
{
    int flags = 0;
    const char *event = add ? "dependency-add" : "dependency-remove";

    if (job->state != FLUX_JOB_STATE_DEPEND
        && job->state != FLUX_JOB_STATE_NEW) {
        errno = EINVAL;
        return -1;
    }
    /* Avoid duplicate dependency-add events on restart
     */
    if (add && grudgeset_contains (job->dependencies, description))
        return 0;
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
    job = current_job (jobtap);
    if (!job || id != job->id) {
        if (!(job = lookup_active_job (jobtap->ctx, id)))
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

static struct job * jobtap_lookup_jobid (flux_plugin_t *p, flux_jobid_t id)
{
    struct jobtap *jobtap;
    struct job *job;
    if (p == NULL
        || !(jobtap = flux_plugin_aux_get (p, "flux::jobtap"))) {
        errno = EINVAL;
        return NULL;
    }
    job = current_job (jobtap);
    if (id == FLUX_JOBTAP_CURRENT_JOB || (job && id == job->id))
        return job;
    return lookup_job (jobtap->ctx, id);
}

static struct job * jobtap_lookup_active_jobid (flux_plugin_t *p,
                                                flux_jobid_t id)
{
    struct job * job = jobtap_lookup_jobid (p, id);
    if (!job || job->state == FLUX_JOB_STATE_INACTIVE) {
        errno = ENOENT;
        return NULL;
    }
    return job;
}

/* Job aux items are not stored in the job aux container directly, to avoid
 * segfaults that might result from registering a destructor resident in a
 * plugin that could be unloaded before the item is destroyed.
 *
 * Instead, each plugin stores one item named "jobtap::<uuid>" which contains
 * an aux container, and the actual items are stored in the inner container.
 * As plugin is being unloaded, the outer container is destroyed before
 * the plugin is destroyed, causing the inner container and its items to be
 * destroyed also.
 */

struct aux_wrap {
    struct aux_item *aux;
    struct jobtap *jobtap;
    char *uuid;
};

static struct aux_wrap *aux_wrap_create (flux_plugin_t *p)
{
    const char *uuid = flux_plugin_get_uuid (p);
    struct aux_wrap *wrap;

    if (!(wrap = calloc (1, sizeof (*wrap) + strlen (uuid) + 1)))
        return NULL;
    wrap->jobtap = flux_plugin_aux_get (p, "flux::jobtap");
    wrap->uuid = (char *)(wrap + 1);
    strcpy (wrap->uuid, uuid);
    return wrap;
}

// flux_free_f signature
static void aux_wrap_destructor (void *item)
{
    struct aux_wrap *wrap = item;
    if (wrap) {
        int saved_errno = errno;
        if (zhashx_lookup (wrap->jobtap->plugins_byuuid, wrap->uuid))
            aux_destroy (&wrap->aux);
        else {
            flux_log (wrap->jobtap->ctx->h,
                      LOG_ERR,
                      "leaking job aux item(s) abandoned by unloaded plugin");
        }
        free (wrap);
        errno = saved_errno;
    }
}

static struct aux_wrap *aux_wrap_get (flux_plugin_t *p,
                                      struct job *job,
                                      bool create)
{
    char wname[64];
    struct aux_wrap *wrap;

    snprintf (wname, sizeof (wname), "jobtap::%s", flux_plugin_get_uuid (p));
    if (!(wrap = job_aux_get (job, wname))) {
        if (!create)
            return NULL;
        if (!(wrap = aux_wrap_create (p))
            || job_aux_set (job, wname, wrap, aux_wrap_destructor) < 0) {
            aux_wrap_destructor (wrap);
            return NULL;
        }
    }
    return wrap;
}

int flux_jobtap_job_aux_set (flux_plugin_t *p,
                             flux_jobid_t id,
                             const char *name,
                             void *val,
                             flux_free_f free_fn)
{
    struct job *job;
    struct aux_wrap *wrap;

    if (!(job = jobtap_lookup_jobid (p, id))
        || !(wrap = aux_wrap_get (p, job, true)))
        return -1;
    return aux_set (&wrap->aux, name, val, free_fn);
}

void * flux_jobtap_job_aux_get (flux_plugin_t *p,
                                flux_jobid_t id,
                                const char *name)
{
    struct job *job;
    struct aux_wrap *wrap;

    if (!(job = jobtap_lookup_jobid (p, id))
        || !(wrap = aux_wrap_get (p, job, false)))
        return NULL;
    return aux_get (wrap->aux, name);
}

int flux_jobtap_job_aux_delete (flux_plugin_t *p,
                                flux_jobid_t id,
                                void *val)
{
    struct job *job;
    struct aux_wrap *wrap;

    if (!(job = jobtap_lookup_jobid (p, id)))
        return -1;
    if ((wrap = aux_wrap_get (p, job, false)))
        aux_delete (&wrap->aux, val);
    return 0;
}

int flux_jobtap_job_set_flag (flux_plugin_t *p,
                              flux_jobid_t id,
                              const char *flag)
{
    struct jobtap *jobtap;
    struct job *job;
    if (!p || !flag || !(jobtap = flux_plugin_aux_get (p, "flux::jobtap"))) {
        errno = EINVAL;
        return -1;
    }
    if (!(job = jobtap_lookup_active_jobid (p, id))) {
        errno = ENOENT;
        return -1;
    }
    if (!job_flag_valid (job, flag))
        return -1;
    return event_job_post_pack (jobtap->ctx->event,
                                job,
                                "set-flags",
                                0,
                                "{s:[s]}",
                                "flags", flag);
}

static int jobtap_job_vraise (struct jobtap *jobtap,
                              struct job *job,
                              const char *type,
                              int severity,
                              const char *fmt,
                              va_list ap)
{
    char note [1024];
    if (vsnprintf (note, sizeof (note), fmt, ap) >= sizeof (note))
        note[sizeof(note) - 2] = '+';
    return raise_job_exception (jobtap->ctx,
                                job,
                                type,
                                severity,
                                jobtap->ctx->owner,
                                note);
}

static int jobtap_job_raise (struct jobtap *jobtap,
                             struct job *job,
                             const char *type,
                             int severity,
                             const char *fmt, ...)
{
    int rc;
    va_list ap;
    va_start (ap, fmt);
    rc = jobtap_job_vraise (jobtap, job, type, severity, fmt, ap);
    va_end (ap);
    return rc;
}

int flux_jobtap_raise_exception (flux_plugin_t *p,
                                 flux_jobid_t id,
                                 const char *type,
                                 int severity,
                                 const char *fmt, ...)
{
    struct jobtap *jobtap;
    struct job *job;
    int rc;
    va_list ap;

    if (!p || !type || !fmt
        || !(jobtap = flux_plugin_aux_get (p, "flux::jobtap"))) {
        errno = EINVAL;
        return -1;
    }
    if (!(job = jobtap_lookup_active_jobid (p, id)))
        return -1;
    va_start (ap, fmt);
    rc = jobtap_job_vraise (jobtap, job, type, severity, fmt, ap);
    va_end (ap);
    return rc;
}

flux_plugin_arg_t * flux_jobtap_job_lookup (flux_plugin_t *p,
                                            flux_jobid_t id)
{
    struct jobtap *jobtap;
    struct job *job;
    if (!p || !(jobtap = flux_plugin_aux_get (p, "flux::jobtap"))) {
        errno = EINVAL;
        return NULL;
    }
    if (!(job = jobtap_lookup_jobid (p, id))) {
        errno = ENOENT;
        return NULL;
    }
    return jobtap_args_create (jobtap, job);
}

int flux_jobtap_get_job_result (flux_plugin_t *p,
                                flux_jobid_t id,
                                flux_job_result_t *rp)
{
    struct jobtap *jobtap;
    struct job *job;
    json_error_t error;
    const char *name = NULL;
    int waitstatus = -1;
    int exception_severity = -1;
    const char *exception_type = NULL;
    flux_job_result_t result = FLUX_JOB_RESULT_FAILED;

    if (!p || !rp || !(jobtap = flux_plugin_aux_get (p, "flux::jobtap"))) {
        errno = EINVAL;
        return -1;
    }
    if (!(job = jobtap_lookup_jobid (p, id))) {
        errno = ENOENT;
        return -1;
    }
    if (job->state != FLUX_JOB_STATE_CLEANUP
        && job->state != FLUX_JOB_STATE_INACTIVE) {
        errno = EINVAL;
        return -1;
    }
    if (json_unpack_ex (job->end_event,
                        &error,
                        0,
                        "{s:s s:{s?i s?s s?i}}",
                        "name", &name,
                        "context",
                        "status", &waitstatus,
                        "type", &exception_type,
                        "severity", &exception_severity) < 0) {
        errno = EINVAL;
        return -1;
    }
    if (streq (name, "finish") && waitstatus == 0)
        result = FLUX_JOB_RESULT_COMPLETED;
    else if (streq (name, "exception")) {
        if (exception_type != NULL) {
            if (streq (exception_type, "cancel"))
                result = FLUX_JOB_RESULT_CANCELED;
            else if (streq (exception_type, "timeout"))
                result = FLUX_JOB_RESULT_TIMEOUT;
        }
    }
    *rp = result;
    return 0;
}

int flux_jobtap_event_post_pack (flux_plugin_t *p,
                                 flux_jobid_t id,
                                 const char *name,
                                 const char *fmt,
                                 ...)
{
    int rc;
    va_list ap;
    struct jobtap *jobtap;
    struct job *job;

    if (!p || !name
        || !(jobtap = flux_plugin_aux_get (p, "flux::jobtap"))) {
        errno = EINVAL;
        return -1;
    }
    if (!(job = jobtap_lookup_active_jobid (p, id)))
        return -1;
    va_start (ap, fmt);
    rc = event_job_post_vpack (jobtap->ctx->event, job, name, 0, fmt, ap);
    va_end (ap);
    return rc;
}

int flux_jobtap_jobspec_update_id_pack (flux_plugin_t *p,
                                        flux_jobid_t id,
                                        const char *fmt,
                                        ...)
{
    int rc = -1;
    va_list ap;
    struct jobtap *jobtap;
    struct job *job;
    json_error_t error;
    json_t *update = NULL;

    if (!p
        || !(jobtap = flux_plugin_aux_get (p, "flux::jobtap"))
        || !(job = jobtap_lookup_active_jobid (p, id))
        || job->state == FLUX_JOB_STATE_RUN
        || job->state == FLUX_JOB_STATE_CLEANUP
        || job->eventlog_readonly) {
        errno = EINVAL;
        return -1;
    }

    /* This interface is only appropriate from outside a jobtap callback,
     * i.e. called asynchronously to update a job. If 'job' is equivalent
     * to the current job at the top of the jobtap stack, return an error.
     */
    if (job == current_job (jobtap)) {
        errno = EINVAL;
        return -1;
    }

    va_start (ap, fmt);
    update = json_vpack_ex (&error, 0, fmt, ap);
    va_end (ap);
    if (!update) {
        errno = EINVAL;
        return -1;
    }
    if (!validate_jobspec_updates (update)) {
        errno = EINVAL;
        goto out;
    }
    /* XXX: should job.validate be called on these updates before posting?
     */
    rc = event_job_post_pack (jobtap->ctx->event,
                              job,
                              "jobspec-update",
                              0,
                              "O",
                              update);
out:
    ERRNO_SAFE_WRAP (json_decref, update);
    return rc;
}

int flux_jobtap_jobspec_update_pack (flux_plugin_t *p, const char *fmt, ...)
{
    int rc = -1;
    int saved_errno;
    va_list ap;
    struct jobtap *jobtap;
    struct job * job;
    json_t *o = NULL;
    json_error_t error;

    if (!p
        || !(jobtap = flux_plugin_aux_get (p, "flux::jobtap"))
        || !(job = current_job (jobtap))
        || job->state == FLUX_JOB_STATE_RUN
        || job->state == FLUX_JOB_STATE_CLEANUP
        || job->eventlog_readonly) {
        errno = EINVAL;
        return -1;
    }
    va_start (ap, fmt);
    o = json_vpack_ex (&error, 0, fmt, ap);
    va_end (ap);
    if (!o) {
        errno = EINVAL;
        return -1;
    }
    if (!validate_jobspec_updates (o)) {
        errno = EINVAL;
        goto out;
    }
    if (!jobtap->jobspec_update)
        jobtap->jobspec_update = json_incref (o);
    else if (json_object_update (jobtap->jobspec_update, o) < 0) {
        errno = EINVAL;
        goto out;
    }
    rc = 0;
out:
    saved_errno = errno;
    json_decref (o);
    errno = saved_errno;
    return rc;
}

int flux_jobtap_job_subscribe (flux_plugin_t *p, flux_jobid_t id)
{
    struct job *job;
    if (!(job = jobtap_lookup_active_jobid (p, id)))
        return -1;
    return job_events_subscribe (job, p);
}

void flux_jobtap_job_unsubscribe (flux_plugin_t *p, flux_jobid_t id)
{
    struct job *job;
    if ((job = jobtap_lookup_active_jobid (p, id)))
        job_events_unsubscribe (job, p);
}

int flux_jobtap_job_event_posted (flux_plugin_t *p,
                                  flux_jobid_t id,
                                  const char *name)
{
    int index;
    struct job * job;
    struct jobtap *jobtap;

    if (!p
        || !name
        || !(jobtap = flux_plugin_aux_get (p, "flux::jobtap"))) {
        errno = EINVAL;
        return -1;
    }
    if (!(job = jobtap_lookup_jobid (p, id))
        || (index = event_index (jobtap->ctx->event, name)) < 0)
        return -1;
    if (job_event_id_test (job, index))
        return 1;
    return 0;
}

static int jobtap_emit_perilog_event (struct jobtap *jobtap,
                                      struct job *job,
                                      bool prolog,
                                      bool start,
                                      const char *description,
                                      int status)
{
    int flags = 0;
    const char *event = prolog ? start ? "prolog-start" : "prolog-finish" :
                                 start ? "epilog-start" : "epilog-finish";

    if (!description) {
        errno = EINVAL;
        return -1;
    }

    /*  prolog events cannot be emitted after a start request is pending.
     *
     *  epilog events cannot be emitted outside of CLEANUP state
     *   and must be emitted before free request is pending.
     */
    if ((prolog && job->start_pending)
        || ((prolog && start) && job->state == FLUX_JOB_STATE_CLEANUP)
        || (!prolog && job->state != FLUX_JOB_STATE_CLEANUP)) {
        errno = EINVAL;
        return -1;
    }
    if (start)
        return event_job_post_pack (jobtap->ctx->event,
                                    job,
                                    event,
                                    flags,
                                    "{s:s}",
                                    "description", description);
    else
        return event_job_post_pack (jobtap->ctx->event,
                                    job,
                                    event,
                                    flags,
                                    "{s:s s:i}",
                                    "description", description,
                                    "status", status);
}

int flux_jobtap_prolog_start (flux_plugin_t *p, const char *description)
{
    struct job * job;
    struct jobtap *jobtap;

    if (!p
        || !(jobtap = flux_plugin_aux_get (p, "flux::jobtap"))
        || !(job = current_job (jobtap))) {
        errno = EINVAL;
        return -1;
    }
    return jobtap_emit_perilog_event (jobtap, job, true, true, description, 0);
}

int flux_jobtap_prolog_finish (flux_plugin_t *p,
                               flux_jobid_t id,
                               const char *description,
                               int status)
{
    struct job * job;
    struct jobtap *jobtap;

    if (!p || !(jobtap = flux_plugin_aux_get (p, "flux::jobtap"))) {
        errno = EINVAL;
        return -1;
    }
    if (!(job = jobtap_lookup_active_jobid (p, id)))
        return -1;
    return jobtap_emit_perilog_event (jobtap,
                                      job,
                                      true,
                                      false,
                                      description,
                                      status);
}

int flux_jobtap_epilog_start (flux_plugin_t *p, const char *description)
{
    struct job * job;
    struct jobtap *jobtap;

    if (!p
        || !(jobtap = flux_plugin_aux_get (p, "flux::jobtap"))
        || !(job = current_job (jobtap))) {
        errno = EINVAL;
        return -1;
    }
    return jobtap_emit_perilog_event (jobtap, job, false, true, description, 0);
}

int flux_jobtap_epilog_finish (flux_plugin_t *p,
                               flux_jobid_t id,
                               const char *description,
                               int status)
{
    struct job * job;
    struct jobtap *jobtap;

    if (!p || !(jobtap = flux_plugin_aux_get (p, "flux::jobtap"))) {
        errno = EINVAL;
        return -1;
    }
    if (!(job = jobtap_lookup_active_jobid (p, id)))
        return -1;
    return jobtap_emit_perilog_event (jobtap,
                                      job,
                                      false,
                                      false,
                                      description,
                                      status);
}

int jobtap_job_update (struct jobtap *jobtap,
                       struct flux_msg_cred cred,
                       struct job *job,
                       const char *key,
                       json_t *value,
                       int *needs_validation,
                       int *require_feasibility,
                       json_t **additional_updates,
                       char **errp)
{
    int rc = -1;
    char topic[128];
    int topiclen = sizeof (topic);
    flux_plugin_arg_t *args = NULL;

    if (snprintf (topic, topiclen, "job.update.%s", key) >= topiclen) {
        error_asprintf (jobtap, job, errp,
                        "topic string overflow");
        return -1;
    }

    if (!(args = jobtap_args_create (jobtap, job))
        || flux_plugin_arg_pack (args,
                                 FLUX_PLUGIN_ARG_IN,
                                 "{s:{s:I s:I} s:s s:O}",
                                 "cred",
                                   "userid", (json_int_t) cred.userid,
                                   "rolemask", (json_int_t) cred.rolemask,
                                 "key", key,
                                 "value", value) < 0
        || flux_plugin_arg_set (args, FLUX_PLUGIN_ARG_OUT, "{}") < 0) {
        error_asprintf (jobtap, job, errp,
                "jobtap_job_update: failed to create args");
        flux_plugin_arg_destroy (args);
        return -1;
    }
    rc = jobtap_stack_call (jobtap, jobtap->plugins, job, topic, args);
    if (rc == 0) {
        /* No plugin handles update of this jobspec key, reject the update.
         */
        error_asprintf (jobtap, job, errp, "update of %s not supported", key);
        rc = -1;
        errno = EINVAL;
    }
    else if (rc < 0) {
        /* Callback failed, check for provided errmsg */
        const char *errmsg;
        if (flux_plugin_arg_unpack (args,
                                    FLUX_PLUGIN_ARG_OUT,
                                    "{s:s}",
                                    "errmsg", &errmsg) < 0) {
            errmsg = "update rejected by job-manager plugin";
        }
        error_asprintf (jobtap, job, errp, "%s", errmsg);
        errno = EINVAL;
    }
    else if (rc > 0) {
        /*  Default is to require further validation by calling job.validate
         *  with the updated jobspec. However, a plugin may note that the
         *  update is already validated or should bypass validation by
         *  setting "validated" in the plugin OUT arguments to a nonzero
         *  value.
         *
         *  Similarly, a plugin can request feasibility check, but the
         *  default is no feasibility will be performed on valid updates.
         */
        int validated = 0;
        int feasibility = 0;
        json_t *updates = NULL;
        if ((rc = flux_plugin_arg_unpack (args,
                                          FLUX_PLUGIN_ARG_OUT,
                                          "{s?i s?i s?o}",
                                          "validated", &validated,
                                          "feasibility", &feasibility,
                                          "updates", &updates) < 0)) {
            error_asprintf (jobtap,
                            job,
                            errp,
                            "failed to unpack update flags");
            return -1;
        }
        if (needs_validation != NULL)
            *needs_validation = !validated;
        if (require_feasibility != NULL)
            *require_feasibility = feasibility;
        if (additional_updates && updates) {
            if (*additional_updates == NULL)
                *additional_updates = json_incref (updates);
            else if (json_object_update (*additional_updates, updates) < 0) {
                error_asprintf (jobtap,
                                job,
                                errp,
                                "failed to apply required extra job updates");
                return -1;
            }
        }
    }
    flux_plugin_arg_destroy (args);
    return rc;
}

int jobtap_validate_updates (struct jobtap *jobtap,
                             struct job *job,
                             json_t *updates,
                             char **errp)
{
    int rc = -1;
    json_t *jobspec_updated = NULL;
    flux_plugin_arg_t *args = NULL;

    if (!(jobspec_updated = job_jobspec_with_updates (job, updates))) {
        error_asprintf (jobtap, job, errp, "update: %s", strerror (errno));
        goto error;
    }

    /*  Create plugin args, then override jobspec with updated version
     */
    if (!(args = jobtap_args_create (jobtap, job))
        || flux_plugin_arg_pack (args,
                                 FLUX_PLUGIN_ARG_IN,
                                 "{s:O}",
                                 "jobspec", jobspec_updated) < 0) {
        error_asprintf (jobtap, job, errp, "update: %s",
                        flux_plugin_arg_strerror (args));
        goto error;
    }

    /*  Call validation stack
     */
    rc = jobtap_stack_call (jobtap,
                            jobtap->plugins,
                            job,
                            "job.validate",
                            args);

    if (rc < 0) {
        const char *errmsg;
        /*
         *  Plugin callback failed, check for errmsg for this job
         *   If plugin did not provide an error message, then construct
         *   a generic error "rejected by plugin".
         */
        if (flux_plugin_arg_unpack (args,
                                    FLUX_PLUGIN_ARG_OUT,
                                    "{s:s}",
                                    "errmsg", &errmsg) < 0)
                errmsg = "rejected by job-manager plugin";
        if ((*errp = strdup (errmsg)) == NULL)
            flux_log (jobtap->ctx->h, LOG_ERR,
                      "jobtap: validate failed to capture errmsg");
        errno = EINVAL;
    }
error:
    ERRNO_SAFE_WRAP (json_decref, jobspec_updated);
    flux_plugin_arg_destroy (args);
    return rc;
}

int flux_jobtap_call (flux_plugin_t *p,
                      flux_jobid_t id,
                      const char *topic,
                      flux_plugin_arg_t *args)
{
    struct jobtap *jobtap;
    struct job *job;

    if (p == NULL
        || !(jobtap = flux_plugin_aux_get (p, "flux::jobtap"))
        || !jobtap->ctx
        || !args
        || strstarts (topic, "job.")) {
        errno = EINVAL;
        return -1;
    }
    if (!(job = jobtap_lookup_jobid (p, id)))
        return -1;
    return jobtap_stack_call (jobtap, jobtap->plugins, job, topic, args);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

