/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* pipeline.c - run jobspec through ingest pipeline: frob | validate */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <flux/core.h>

#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"

#include "util.h"
#include "workcrew.h"
#include "pipeline.h"

struct pipeline {
    flux_t *h;
    struct workcrew *validate;
    int process_count;
    flux_watcher_t *shutdown_timer;
};

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
              "shutdown timed out with %d validators active",
              pl->process_count);
    flux_reactor_stop (r);
}

void pipeline_shutdown (struct pipeline *pl)
{
    pl->process_count = workcrew_stop_notify (pl->validate, exit_cb, pl);
    if (pl->process_count == 0)
        flux_reactor_stop (flux_get_reactor (pl->h));
    else {
        flux_timer_watcher_reset (pl->shutdown_timer, shutdown_timeout, 0.);
        flux_watcher_start (pl->shutdown_timer);
    }

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

int pipeline_process_job (struct pipeline *pl,
                          struct job *job,
                          flux_future_t **fp,
                          flux_error_t *error)
{
    if ((!pl->validate || (job->flags & FLUX_JOB_NOVALIDATE))) {
        *fp = NULL;
    }
    else {
        flux_future_t *f;
        if (!(f = validate_job (pl, job, error)))
            return -1;
        *fp = f;
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
                            "{s?{s?o s?o s?b !}}",
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
                        flux_error_t *error)
{
    flux_error_t conf_error;
    json_t *ingest = NULL;
    char *validator_plugins = NULL;
    char *validator_args = NULL;
    bool disable_validator = false;

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
                                &disable_validator,
                                error) < 0)
        return -1;

    /* Process module command line
     */
    for (int i = 0; i < argc; i++) {
        if (!strncmp (argv[i], "validator-args=", 15)) {
            free (validator_args);
            validator_args = strdup (argv[i] + 15);
        }
        else if (!strncmp (argv[i], "validator-plugins=", 18)) {
            free (validator_plugins);
            validator_plugins = strdup (argv[i] + 18);
        }
        else if (!strcmp (argv[i], "disable-validator"))
            disable_validator = 1;
    }

    /* Take action on configuration update.
     */
    if (disable_validator) {
        if (pl->validate) {
            errprintf (error, "Unable to disable validator at runtime");
            errno = EINVAL;
            goto error;
        }
        // Checked for by t2111-job-ingest-config.t
        flux_log (pl->h, LOG_DEBUG, "Disabling job validator");
    }
    else {
        if (!pl->validate) {
            if (!(pl->validate = workcrew_create (pl->h))) {
                errprintf (error,
                           "Error creating validator workcrew: %s",
                           strerror (errno));
                goto error;
            }
        }
        // Checked for by t2111-job-ingest-config.t
        flux_log (pl->h,
                  LOG_DEBUG,
                  "configuring with plugins=%s, args=%s",
                  validator_plugins,
                  validator_args);
        if (workcrew_configure (pl->validate,
                                "job-validator",
                                validator_plugins,
                                validator_args) < 0) {
            errprintf (error,
                       "Error (re-)configuring validator workcrew: %s",
                       strerror (errno));
            goto error;
        }
    }

    free (validator_plugins);
    free (validator_args);
    return 0;
error:
    ERRNO_SAFE_WRAP (free, validator_plugins);
    ERRNO_SAFE_WRAP (free, validator_args);
    return -1;
}

json_t *pipeline_stats_get (struct pipeline *pl)
{
    json_t *o = NULL;
    if (pl) {
        json_t *vo = workcrew_stats_get (pl->validate);
        o = json_pack ("{s:O}",
                       "validator", vo);
        json_decref (vo);
    }
    return o ? o : json_null ();
}

void pipeline_destroy (struct pipeline *pl)
{
    if (pl) {
        int saved_errno = errno;
        workcrew_destroy (pl->validate);
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
    return pl;
error:
    pipeline_destroy (pl);
    return NULL;
}

// vi:ts=4 sw=4 expandtab
