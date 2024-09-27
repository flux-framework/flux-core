/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* pipeline.c - run jobspec through ingest pipeline: frobnicator | validator */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <flux/core.h>

#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"

#include "util.h"
#include "workcrew.h"
#include "pipeline.h"

struct pipeline {
    flux_t *h;
    struct workcrew *validate;
    struct workcrew *frobnicate;
    int process_count;
    flux_watcher_t *shutdown_timer;
    bool validator_bypass;
    bool frobnicate_enable;
};

static const char *cmd_validator = "job-validator";
static const char *cmd_frobnicator = "job-frobnicator";


/* Timeout (seconds) to wait for workers to terminate when
 * stopped by closing their stdin.  If the timer expires, stop the reactor
 * and allow workcrew_destroy() to signal them.
 */
static const double shutdown_timeout = 5.;

static void exit_cb (void *arg)
{
    struct pipeline *pl = arg;

    if (--pl->process_count == 0) {
        flux_watcher_stop (pl->shutdown_timer);
        flux_reactor_stop (flux_get_reactor (pl->h));
    }
}

static void shutdown_timeout_cb (flux_reactor_t *r,
                                 flux_watcher_t *w,
                                 int revents,
                                 void *arg)
{
    struct pipeline *pl = arg;

    flux_log (pl->h,
              LOG_ERR,
              "shutdown timed out with %d workers active",
              pl->process_count);
    flux_reactor_stop (r);
}

void pipeline_shutdown (struct pipeline *pl)
{
    pl->process_count = workcrew_stop_notify (pl->validate, exit_cb, pl);
    pl->process_count += workcrew_stop_notify (pl->frobnicate, exit_cb, pl);
    if (pl->process_count == 0)
        flux_reactor_stop (flux_get_reactor (pl->h));
    else {
        flux_timer_watcher_reset (pl->shutdown_timer, shutdown_timeout, 0.);
        flux_watcher_start (pl->shutdown_timer);
    }

}

static bool validator_bypass (struct pipeline *pl, struct job *job)
{
    if ((pl->validator_bypass || (job->flags & FLUX_JOB_NOVALIDATE)))
        return true;
    return false;
}

static flux_future_t *validate_job (struct pipeline *pl,
                                    struct job *job,
                                    flux_error_t *error)
{
    json_t *input;
    flux_future_t *f;

    if (!(input = job_json_object (job, error)))
        return NULL;
    if (!(f = workcrew_process_job (pl->validate, input))) {
        errprintf (error, "Error passing job to validator");
        goto error;
    }
    json_decref (input);
    return f;
error:
    ERRNO_SAFE_WRAP (json_decref, input);
    return NULL;
}

static flux_future_t *frobnicate_job (struct pipeline *pl,
                                      struct job *job,
                                      flux_error_t *error)
{
    json_t *input;
    flux_future_t *f;

    if (!(input = job_json_object (job, error)))
        return NULL;
    if (!(f = workcrew_process_job (pl->frobnicate, input))) {
        errprintf (error, "Error passing job to frobnicator");
        goto error;
    }
    json_decref (input);
    return f;
error:
    ERRNO_SAFE_WRAP (json_decref, input);
    return NULL;
}

static void frobnicate_continuation (flux_future_t *f1, void *arg)
{
    struct pipeline *pl = arg;
    struct job *job = flux_future_aux_get (f1, "job");
    const char *s;
    json_t *jobspec;
    const char *errmsg = NULL;
    flux_error_t error;

    if (flux_future_get (f1, (const void **)&s) < 0) {
        errmsg = future_strerror (f1, errno);
        goto error;
    }

    if (!(jobspec = json_loads (s, 0, NULL))) {
        errmsg = "error decoding jobspec from frobnicator";
        errno = EINVAL;
        goto error;
    }
    json_decref (job->jobspec);
    job->jobspec = jobspec;

    if (!validator_bypass (pl, job)) {
        flux_future_t *f2;

        if (!(f2 = validate_job (pl, job, &error))) {
            errmsg = error.text;
            goto error;
        }
        if (flux_future_continue (f1, f2) < 0) {
            flux_future_destroy (f2);
            errmsg = "error continuing validator";
            goto error;
        }
    }
    goto done;
error:
    flux_future_continue_error (f1, errno, errmsg);
done:
    flux_future_destroy (f1);
}

/* N.B. this function could be a little simpler if futures for the pipeline
 * stages were unconditionally chained;  instead, minimize overhead for:
 * - frobnicator not configured
 * - frobnicator not configured AND validator bypassed
 */
int pipeline_process_job (struct pipeline *pl,
                          struct job *job,
                          flux_future_t **fp,
                          flux_error_t *error)
{
    if (pl->frobnicate_enable) {
        flux_future_t *f1;
        flux_future_t *f_comp;

        if (!(f1 = frobnicate_job (pl, job, error))
            || flux_future_aux_set (f1, "job", job, NULL) < 0
            || !(f_comp = flux_future_and_then (f1,
                                                frobnicate_continuation,
                                                pl))) {
            flux_future_destroy (f1);
            return -1;
        }
        *fp = f_comp;
    }
    else {
        flux_future_t *f;

        if (validator_bypass (pl, job))
            *fp = NULL;
        else {
            if (!(f = validate_job (pl, job, error)))
                return -1;
            *fp = f;
        }
    }
    return 0;
}

static int unpack_ingest_subtable (json_t *o,
                                   const char *name,
                                   char **pluginsp,
                                   char **argsp,
                                   bool *disablep,
                                   flux_error_t *error)
{
    json_error_t json_error;
    json_t *op = NULL;
    json_t *oa = NULL;
    char *plugins = NULL;
    char *args = NULL;
    int disable = 0;

    if (o) {
        if (json_unpack_ex (o,
                            &json_error,
                            0,
                            "{s?{s?o s?o s?b}}",
                            name,
                              "args", &oa,
                              "plugins", &op,
                              "disable", &disable) < 0) {
            errprintf (error,
                       "error parsing [ingest.%s] config table: %s",
                       name,
                       json_error.text);
            errno = EINVAL;
            return -1;
        }
        if (!disablep && disable) {
            errprintf (error, "[ingest.%s]: 'disable' key is unknown", name);
            errno = EINVAL;
            return -1;
        }
        if (op) {
            if (!(plugins = util_join_arguments (op))) {
                errprintf (error, "error in [ingest.%s] plugins array", name);
                goto error;
            }
        }
        if (oa) {
            if (!(args = util_join_arguments (oa))) {
                errprintf (error, "error in [ingest.%s] args array", name);
                goto error;
            }
        }
    }
    *pluginsp = plugins;
    *argsp = args;
    *disablep = disable ? true : false;
    return 0;
error:
    ERRNO_SAFE_WRAP (free, args);
    ERRNO_SAFE_WRAP (free, plugins);
    return -1;
}

int pipeline_configure (struct pipeline *pl,
                        const flux_conf_t *conf,
                        int argc,
                        char **argv,
                        const char *bufsize,
                        flux_error_t *error)
{
    flux_error_t conf_error;
    json_t *ingest = NULL;
    char *validator_plugins = NULL;
    char *validator_args = NULL;
    char *frobnicator_plugins = NULL;
    char *frobnicator_args = NULL;
    bool frobnicator_bypass = false;
    int rc = -1;

    /* Process toml
     */
    if (flux_conf_unpack (conf,
                          &conf_error,
                          "{s?o}",
                          "ingest", &ingest) < 0) {
        errprintf (error,
                   "error parsing [ingest] config table: %s",
                   conf_error.text);
        return -1;
    }
    if (unpack_ingest_subtable (ingest,
                                "validator",
                                &validator_plugins,
                                &validator_args,
                                &pl->validator_bypass,
                                error) < 0)
        return -1;
    if (unpack_ingest_subtable (ingest,
                                "frobnicator",
                                &frobnicator_plugins,
                                &frobnicator_args,
                                &frobnicator_bypass,
                                error) < 0)
        return -1;

    /* Process module command line
     */
    for (int i = 0; i < argc; i++) {
        if (strstarts (argv[i], "validator-args=")) {
            free (validator_args);
            validator_args = strdup (argv[i] + 15);
        }
        else if (strstarts (argv[i], "validator-plugins=")) {
            free (validator_plugins);
            validator_plugins = strdup (argv[i] + 18);
        }
        else if (streq (argv[i], "disable-validator"))
            pl->validator_bypass = true;
    }

    /* Enable the frobnicator if not bypassed AND either explicitly configured
     * or implicitly required by queues or jobspec defaults configuration.
     * See flux-framework/flux-core#4598.
     */
    pl->frobnicate_enable = false;
    if (!frobnicator_bypass) {
        if (frobnicator_plugins && strlen (frobnicator_plugins) > 0)
            pl->frobnicate_enable = true;
        else {
            json_t *defaults = NULL;
            json_t *queues = NULL;
            (void)flux_conf_unpack (conf,
                                    NULL,
                                    "{s?{s?{s?o}} s?o}",
                                    "policy",
                                      "jobspec",
                                        "defaults", &defaults,
                                    "queues", &queues);
            if (defaults || queues)
                pl->frobnicate_enable = true;
        }
    }

    if (workcrew_configure (pl->frobnicate,
                            cmd_frobnicator,
                            frobnicator_plugins,
                            frobnicator_args,
                            bufsize) < 0) {
        errprintf (error,
                   "Error (re-)configuring frobnicator workcrew: %s",
                   strerror (errno));
        goto error;
    }

    // Checked for by t2111-job-ingest-config.t
    flux_log (pl->h,
              LOG_DEBUG,
              "configuring validator with plugins=%s, args=%s (%s)",
              validator_plugins,
              validator_args,
              pl->validator_bypass ? "disabled" : "enabled");
    if (workcrew_configure (pl->validate,
                            cmd_validator,
                            validator_plugins,
                            validator_args,
                            bufsize) < 0) {
        errprintf (error,
                   "Error (re-)configuring validator workcrew: %s",
                   strerror (errno));
        goto error;
    }
    rc = 0;
error:
    ERRNO_SAFE_WRAP (free, validator_plugins);
    ERRNO_SAFE_WRAP (free, validator_args);
    ERRNO_SAFE_WRAP (free, frobnicator_plugins);
    ERRNO_SAFE_WRAP (free, frobnicator_args);
    return rc;
}

json_t *pipeline_stats_get (struct pipeline *pl)
{
    json_t *o = NULL;
    if (pl) {
        json_t *fo = workcrew_stats_get (pl->frobnicate);
        json_t *vo = workcrew_stats_get (pl->validate);
        o = json_pack ("{s:O s:O}",
                       "frobnicator", fo,
                       "validator", vo);
        json_decref (fo);
        json_decref (vo);
    }
    return o ? o : json_null ();
}

void pipeline_destroy (struct pipeline *pl)
{
    if (pl) {
        int saved_errno = errno;
        workcrew_destroy (pl->validate);
        workcrew_destroy (pl->frobnicate);
        flux_watcher_destroy (pl->shutdown_timer);
        free (pl);
        errno = saved_errno;
    }
}

struct pipeline *pipeline_create (flux_t *h)
{
    struct pipeline *pl;
    flux_reactor_t *r = flux_get_reactor (h);

    if (!(pl = calloc (1, sizeof (*pl))))
        return NULL;
    pl->h = h;
    if (!(pl->shutdown_timer = flux_timer_watcher_create (r,
                                                          0.,
                                                          0.,
                                                          shutdown_timeout_cb,
                                                          pl)))
        goto error;
    if (!(pl->validate = workcrew_create (pl->h))
        || workcrew_configure (pl->validate,
                               cmd_validator,
                               NULL,
                               NULL,
                               NULL) < 0)
        goto error;
    if (!(pl->frobnicate = workcrew_create (pl->h))
        || workcrew_configure (pl->frobnicate,
                               cmd_frobnicator,
                               NULL,
                               NULL,
                               NULL) < 0)
        goto error;
    return pl;
error:
    pipeline_destroy (pl);
    return NULL;
}

// vi:ts=4 sw=4 expandtab
