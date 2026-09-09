/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* killbot - clear out preemptible jobs under job pressure
 *
 * This is a workaround for schedulers that don't do preemption.
 * It's necessarily dumber than a scheduler could be because it doesn't
 * know the impact on the schedule when it guesses which jobs to preempt.
 *
 * Internal operation
 *
 * Two sets of jobs are maintained:
 * - victims: jobs in RUN state with preemptible-after set to any value.
 *   These jobs are candidate victims for the killbot, though not all
 *   may be eligible yet.
 * - victors: jobs in SCHED state with preemptible-after unset or > 0.
 *   These jobs should run in preference to any eligible victims.
 *
 * Kill mode is activated when the victors and victims sets are both non-empty.
 * Kill mode is deactivated when one or both sets are empty
 * During kill mode, a kill handler runs periodically, dispatching eligible
 * victims so that victors can run.
 *
 * Configurable in [job-manager.killbot]:
 * - kill-after: (seconds) the longest tolerable victor wait time.  Use a
 *   conservative estimate of scheduler loop time.
 * - kill-repeat: (seconds): the time between invocations of the kill handler.
 *   This should allow time for the system to settle after victim(s) have been
 *   killed, including epilog, houesekeeping, and scheduler loop time.
 * - handler: (string) the algorithm that selects victims and dispatches them.
 *
 * The available handlers are, thus far:
 * - overkill: all victims are killed on first invocation
 * - onekill: one victim is killed on each invocation
 *
 * The intent is that this plugin can be improved incrementally by focusing
 * on the handlers while ignoring the framework that does timers and tracking.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdarg.h>
#include <jansson.h>
#include <math.h>
#include <flux/core.h>
#include <flux/jobtap.h>

#include "src/common/libjob/idf58.h"
#include "src/common/libjob/job_hash.h"
#include "src/common/libjob/jj.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "ccan/str/str.h"
#include "ccan/array_size/array_size.h"

struct killbot;

/* Returns the number of victims killed.
 */
typedef int (*kill_handler_f)(struct killbot *killbot);

struct job_entry {
    flux_jobid_t id;
    double preemptible_after;   // -1 if unset
    double t_run;
    double t_sched;
    /* data to help victim selection */
    char *queue;
    struct jj_counts counts;
};

struct kill_handler {
    const char *name;
    kill_handler_f cb;
};

struct killbot {
    flux_plugin_t *p;
    flux_t *h;
    flux_reactor_t *reactor;
    zhashx_t *victims;
    zhashx_t *victors;
    flux_watcher_t *kill_timer;
    flux_watcher_t *age_timer;
    double kill_after;
    double kill_repeat;
    const struct kill_handler *handler;
    int kills;
};

static int overkill_killer (struct killbot *killbot);
static int onekill_killer (struct killbot *killbot);

/* Table of available kill handlers.
 */
static const struct kill_handler khtab[] = {
    { .name = "overkill", .cb = overkill_killer, },
    { .name = "onekill", .cb = onekill_killer, },
};

static const struct kill_handler *default_handler = &khtab[0];
static const double default_kill_after = 30;
static const double default_kill_repeat = 60;


static const struct kill_handler *find_handler (const char *name)
{
    for (int i = 0; i < ARRAY_SIZE (khtab); i++)
        if (streq (name, khtab[i].name))
            return &khtab[i];
    return NULL;
}

static void job_entry_destroy (struct job_entry *job)
{
    if (job) {
        int saved_errno = errno;
        free (job->queue);
        free (job);
        errno = saved_errno;
    }
}

static struct job_entry *job_entry_create (flux_jobid_t id,
                                           double preemptible_after)
{
    struct job_entry *job;

    if (!(job = calloc (1, sizeof (*job))))
        return NULL;
    job->id = id;
    job->preemptible_after = preemptible_after;
    return job;
}

static void job_entry_destructor (void **item)
{
    if (item) {
        job_entry_destroy (*item);
        *item = NULL;
    }
}

static int parse_jobspec_sysattr (json_t *jobspec,
                                  flux_error_t *error,
                                  const char *fmt,
                                  ...)
{
    va_list ap;
    json_error_t jerror;
    json_t *attrs;
    int rc;

    if (json_unpack_ex (jobspec,
                        &jerror,
                        0,
                        "{s:{s:o}}",
                        "attributes",
                          "system", &attrs) < 0) {
        errprintf (error, "%s", jerror.text);
        return -1;
    }
    va_start (ap, fmt);
    rc = json_vunpack_ex (attrs, &jerror, 0, fmt, ap);
    va_end (ap);
    if (rc < 0) {
        errprintf (error, "attributes.system %s", jerror.text);
        return -1;
    }
    return 0;
}

static int job_entry_enhance (struct job_entry *job,
                              json_t *jobspec,
                              flux_error_t *error)
{
    const char *queue = NULL;
    if (jj_get_counts_json (jobspec, &job->counts) < 0) {
        errprintf (error, "%s", job->counts.error);
        return -1;
    }
    if (parse_jobspec_sysattr (jobspec, error, "{s?s}", "queue", &queue) < 0)
        return -1;
    if (queue) {
        if (!(job->queue = strdup (queue))) {
            errprintf (error, "out of memory");
            return -1;
        }
    }
    return 0;
}

static bool is_eligible (struct job_entry *job, double now)
{
    double run_time = now - job->t_run;
    if (job->preemptible_after <= run_time)
        return true;
    return false;
}

/* Like streq() except s1 == s2 == NULL is also a match.
 */
static bool strmatch (const char *s1, const char *s2)
{
    if (!s1 && !s2)
        return true;
    if (!s1 || !s2)
        return false;
    return streq (s1, s2);
}

/* Count how many nodes are requested for a given queue.
 * queue=NULL is the anonymous queue.
 * If a job is requesting zero nodes, assume nodes are underspecified
 * and there will be at least one.
 */
static int count_nodes_byqueue (zhashx_t *jobs, const char *queue)
{
    struct job_entry *job;
    int count = 0;

    job = zhashx_first (jobs);
    while (job) {
        if (strmatch (job->queue, queue))
            count += job->counts.nnodes > 0 ? job->counts.nnodes : 1;
        job = zhashx_next (jobs);
    }
    return count;
}

/* Preempt job 'id'.
 * WARNING: state_change_cb() can be called from flux_jobtap_raise_exception(),
 * so do not call this function during non-deletion-safe hash iteration.
 */
static void preempt_job (struct killbot *killbot, flux_jobid_t id)
{
    if (flux_jobtap_raise_exception (killbot->p,
                                     id,
                                     "preempt",
                                     0,
                                     "killbot/%s",
                                     killbot->handler->name) < 0)
        flux_log_error (killbot->h, "killbot: jobtap_raise_exception");
    killbot->kills++;
}

/* overkill - kill all victims in one go
 *
 * Skip victims in queues that have no pressure.
 */
static int overkill_killer (struct killbot *killbot)
{
    double now = flux_reactor_now (killbot->reactor);
    struct job_entry *job;
    int count = 0;
    flux_jobid_t *ids;

    ids = calloc (zhashx_size (killbot->victims), sizeof (ids[0]));
    if (!ids)
        return 0;
    job = zhashx_first (killbot->victims);
    while (job) {
        if (!is_eligible (job, now))
            goto next;
        if (count_nodes_byqueue (killbot->victors, job->queue) == 0)
            goto next;
        ids[count++] = job->id;
next:
        job = zhashx_next (killbot->victims);
    }
    for (int i = 0; i < count; i++)
        preempt_job (killbot, ids[i]);
    free (ids);
    return count;
}

/* onekill - kill one victim on each invocation.
 *
 * Victims are selected at random.
 * Skip victims in queues that have no pressure.
 */
static int onekill_killer (struct killbot *killbot)
{
    double now = flux_reactor_now (killbot->reactor);
    struct job_entry *job;
    int count = 0;

    job = zhashx_first (killbot->victims);
    while (job) {
        if (!is_eligible (job, now))
            goto next;
        if (count_nodes_byqueue (killbot->victors, job->queue) == 0)
            goto next;
         preempt_job (killbot, job->id);
         break;
next:
        job = zhashx_next (killbot->victims);
    }
    return count;
}

/* Find victim jobs eligible for preemption and return a job count.
 * If none are found, set 'min_wait' to the number of seconds until at least
 * will will eligible, or INFINITY if there will be none.
 */
static int count_eligible (struct killbot *killbot, double *min_wait)
{
    double now = flux_reactor_now (killbot->reactor);
    struct job_entry *job;
    int count = 0;
    double min_wait_time = INFINITY;

    job = zhashx_first (killbot->victims);
    while (job) {
        double wait_time = job->preemptible_after - (now - job->t_run);
        if (wait_time <= 0)
            count++;
        else if (wait_time < min_wait_time)
            min_wait_time = wait_time;
        job = zhashx_next (killbot->victims);
    }
    if (min_wait && count == 0)
        *min_wait = min_wait_time;
    return count;
}

static void update_timers_if_needed (struct killbot *killbot)
{
    double min_wait = INFINITY;
    int eligible_victim_count = count_eligible (killbot, &min_wait);
    int victor_count = zhashx_size (killbot->victors);

    /* stop/start the kill timer */
    if (flux_watcher_is_active (killbot->kill_timer)) {
        if (victor_count == 0 || eligible_victim_count == 0)
            flux_watcher_stop (killbot->kill_timer);
    }
    else {
        if (victor_count > 0 && eligible_victim_count > 0) {
            flux_timer_watcher_reset (killbot->kill_timer,
                                      killbot->kill_after,
                                      killbot->kill_repeat);
            flux_watcher_start (killbot->kill_timer);
        }
    }
    /* stop/start the age timer */
    if (flux_watcher_is_active (killbot->age_timer)) {
        if (flux_watcher_is_active (killbot->kill_timer))
            flux_watcher_stop (killbot->age_timer);
    }
    else {
        if (!flux_watcher_is_active (killbot->kill_timer)
            && min_wait < INFINITY) {
            flux_timer_watcher_reset (killbot->age_timer, min_wait, 0);
            flux_watcher_start (killbot->age_timer);
        }
    }
}

static void kill_timer_cb (flux_reactor_t *r,
                           flux_watcher_t *w,
                           int revents,
                           void *arg)
{
    struct killbot *killbot = arg;
    int count = killbot->handler->cb (killbot);
    flux_log (killbot->h,
              LOG_DEBUG,
              "killbot: %s dispatched %d victims",
              killbot->handler->name,
              count);
    update_timers_if_needed (killbot);
}

static void age_timer_cb (flux_reactor_t *r,
                          flux_watcher_t *w,
                          int revents,
                          void *arg)
{
    struct killbot *killbot = arg;
    update_timers_if_needed (killbot);
}

static int state_change_cb (flux_plugin_t *p,
                            const char *topic,
                            flux_plugin_arg_t *args,
                            void *arg)
{
    struct killbot *killbot = flux_plugin_aux_get (p, "killbot");
    flux_jobid_t id;
    flux_job_state_t state;
    json_t *jobspec;
    flux_error_t error;
    struct job_entry *job = NULL;
    double pa = -1;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I s:i s?o}",
                                "id", &id,
                                "state", &state,
                                "jobspec", &jobspec) < 0) {
        return flux_jobtap_error (p,
                                  args,
                                  "killbot: error unpacking plugin args: %s",
                                  flux_plugin_arg_strerror (args));
    }
    if (state == FLUX_JOB_STATE_SCHED || state == FLUX_JOB_STATE_RUN) {
        if (parse_jobspec_sysattr (jobspec,
                                   &error,
                                   "{s?F}",
                                   "preemptible-after", &pa) < 0) {
            return flux_jobtap_error (p,
                                      args,
                                      "killbot: error parsing jobspec: %s",
                                      error.text);
        }
    }
    switch (state) {
        case FLUX_JOB_STATE_SCHED:
            if (pa == -1 || pa > 0) {
                if (!(job = job_entry_create (id, pa))
                    || zhashx_insert (killbot->victors, &job->id, job) < 0) {
                    job_entry_destroy (job);
                    goto tracking_error;
                }
                job->t_sched = flux_reactor_now (killbot->reactor);
            }
            break;
        case FLUX_JOB_STATE_RUN:
            if (zhashx_lookup (killbot->victors, &id)) {
                zhashx_delete (killbot->victors, &id);
            }
            if (pa >= 0) {
                if (!(job = job_entry_create (id, pa))
                    || zhashx_insert (killbot->victims, &job->id, job) < 0) {
                    job_entry_destroy (job);
                    goto tracking_error;
                }
                job->t_run = flux_reactor_now (killbot->reactor);
            }
            break;
        case FLUX_JOB_STATE_CLEANUP:
            zhashx_delete (killbot->victors, &id);
            zhashx_delete (killbot->victims, &id);
            break;
        default:
            break;
    }
    /* Expand job data if an entry was just created.
     * This data is used for heuristics only - just warn on failure.
     */
    if (job) {
        if (job_entry_enhance (job, jobspec, &error) < 0) {
            flux_log (killbot->h,
                      LOG_WARNING,
                      "killbot %s %s: warning: %s",
                      topic,
                      idf58 (id),
                      error.text);
        }
    }
    update_timers_if_needed (killbot);
    return 0;
tracking_error:
    return flux_jobtap_error (p,
                              args,
                              "killbot %s: error tracking jobid %s",
                              topic,
                              idf58 (id));
}

static int killbot_config (struct killbot *killbot,
                           json_t *conf,
                           flux_error_t *error)
{
    json_error_t jerror;
    const char *handler_name = NULL;
    const struct kill_handler *handler = NULL;
    double kill_after = INFINITY;
    double kill_repeat = INFINITY;

    if (conf) {
        if (json_unpack_ex (conf,
                            &jerror,
                            0,
                            "{s?s s?F s?F !}",
                            "handler", &handler_name,
                            "kill-after", &kill_after,
                            "kill-repeat", &kill_repeat) < 0) {
            errprintf (error, "config parse error: %s", jerror.text);
            return -1;
        }
    }
    if (kill_after != INFINITY) {
        if (kill_after < 0) { // 0=immediate
            errprintf (error, "kill-after must be >= 0");
            return -1;
        }
    }
    if (kill_repeat != INFINITY) {
        if (kill_repeat <= 0) { // 0=never
            errprintf (error, "kill-repeat must be > 0");
            return -1;
        }
    }
    if (handler_name) {
        if (!(handler = find_handler (handler_name))) {
            errprintf (error, "unknown handler '%s'", handler_name);
            return -1;
        }
    }
    if (kill_after != INFINITY)
        killbot->kill_after = kill_after;
    else
        killbot->kill_after = default_kill_after;
    if (kill_repeat != INFINITY)
        killbot->kill_repeat = kill_repeat;
    else
        killbot->kill_repeat = default_kill_repeat;
    if (handler)
        killbot->handler = handler;
    else
        killbot->handler = default_handler;
    return 0;
}

static int conf_update_cb (flux_plugin_t *p,
                           const char *topic,
                           flux_plugin_arg_t *args,
                           void *arg)
{
    struct killbot *killbot = flux_plugin_aux_get (p, "killbot");
    flux_error_t error;
    json_t *conf = NULL;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:{s?{s?o}}}",
                                "conf",
                                  "job-manager",
                                    "killbot", &conf) < 0) {
        errprintf (&error,
                   "error unpacking config.update arguments: %s",
                   flux_plugin_arg_strerror (args));
        goto error;
    }
    if (killbot_config (killbot, conf, &error) < 0)
        goto error;
    return 0;
error:
    return flux_jobtap_error (p, args, "killbot: %s", error.text);
}

static json_t *create_query_object (struct killbot *kb, flux_error_t *error)
{
    json_t *o;
    json_error_t jerror;
    o = json_pack_ex (&jerror,
                      0,
                      "{s:i s:b s:b s:f s:f s:s s:i}",
                      "eligible-victims", count_eligible (kb, NULL),
                      "kill-active", flux_watcher_is_active (kb->kill_timer),
                      "age-active", flux_watcher_is_active (kb->age_timer),
                      "kill-after", kb->kill_after,
                      "kill-repeat", kb->kill_repeat,
                      "handler", kb->handler ? kb->handler->name : "none",
                      "kills", kb->kills);
    if (!o) {
        errprintf (error, "%s", jerror.text);
        return NULL;
    }
    return o;
}

static int query_cb (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *args,
                     void *arg)
{
    struct killbot *killbot = flux_plugin_aux_get (p, "killbot");
    flux_t *h = flux_jobtap_get_flux (p);
    flux_error_t error;
    json_t *query;

    if (!(query = create_query_object (killbot, &error))) {
        flux_log (h,
                  LOG_ERR,
                  "killbot: error preparing query response: %s",
                  error.text);
        goto done;
    }

    if (flux_plugin_arg_pack (args, FLUX_PLUGIN_ARG_OUT, "O", query) < 0) {
        flux_log (h,
                  LOG_ERR,
                  "killbot: error packing query return argument: %s",
                   flux_plugin_arg_strerror (args));
        goto done;
    }
done:
    json_decref (query);
    return 0;
}

static void killbot_destroy (struct killbot *killbot)
{
    if (killbot) {
        int saved_errno = errno;
        flux_watcher_destroy (killbot->kill_timer);
        flux_watcher_destroy (killbot->age_timer);
        zhashx_destroy (&killbot->victims);
        zhashx_destroy (&killbot->victors);
        free (killbot);
        errno = saved_errno;
    }
}

static struct killbot *killbot_create (flux_plugin_t *p)
{
    struct killbot *killbot;

    if (!(killbot = calloc (1, sizeof (*killbot))))
        return NULL;
    killbot->p = p;
    killbot->h = flux_jobtap_get_flux (p);
    killbot->reactor = flux_get_reactor (killbot->h);
    killbot->kill_after = default_kill_after;
    killbot->kill_repeat =default_kill_repeat;

    if (!(killbot->victims = job_hash_create ())
        || !(killbot->victors = job_hash_create ())) {
        errno = ENOMEM;
        goto error;
    }
    zhashx_set_destructor (killbot->victims, job_entry_destructor);
    zhashx_set_destructor (killbot->victors, job_entry_destructor);
    killbot->handler = default_handler;
    if (!(killbot->kill_timer = flux_timer_watcher_create (killbot->reactor,
                                                           killbot->kill_after,
                                                           killbot->kill_repeat,
                                                           kill_timer_cb,
                                                           killbot)))
        goto error;
    if (!(killbot->age_timer = flux_timer_watcher_create (killbot->reactor,
                                                          0,
                                                          0,
                                                          age_timer_cb,
                                                          killbot)))
        goto error;
    return killbot;
error:
    killbot_destroy (killbot);
    return NULL;
}


static const struct flux_plugin_handler tab[] = {
    { "job.state.sched", state_change_cb, NULL },
    { "job.state.run", state_change_cb, NULL },
    { "job.state.cleanup", state_change_cb, NULL },
    { "conf.update", conf_update_cb, NULL },
    { "plugin.query", query_cb, NULL },
    { 0 },
};

int flux_plugin_init (flux_plugin_t *p)
{
    struct killbot *killbot;

    if (!(killbot = killbot_create (p))
        || flux_plugin_aux_set (p,
                                "killbot",
                                killbot,
                                (flux_free_f)killbot_destroy) < 0) {
        killbot_destroy (killbot);
        return -1;
    }
    return flux_plugin_register (p, "killbot", tab);
}

// vi:ts=4 sw=4 expandtab
