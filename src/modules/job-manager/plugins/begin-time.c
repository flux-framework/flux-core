/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/*  begin-time: Builtin job-manager begin-time dependency plugin */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <time.h>
#include <math.h>
#include <jansson.h>

#include <flux/core.h>
#include <flux/jobtap.h>

#include "src/common/libjob/idf58.h"

struct begin_time_arg {
    flux_plugin_t *p;
    flux_watcher_t *w;
    flux_jobid_t id;
    double begin_time;
    char desc [128];
};

static void begin_time_arg_destroy (struct begin_time_arg *b)
{
    if (b) {
        flux_watcher_destroy (b->w);
        free (b);
    }
}

static struct begin_time_arg * begin_time_arg_create (flux_plugin_t *p,
                                                      flux_jobid_t id,
                                                      double begin_time)
{
    struct begin_time_arg *b = NULL;
    int len = sizeof (b->desc);

    if (!(b = calloc (1, sizeof (*b)))
        || snprintf (b->desc, len, "begin-time=%.3f", begin_time) >= len)
        goto err;
    b->p = p;
    b->id = id;
    b->begin_time = begin_time;
    return b;
err:
    begin_time_arg_destroy (b);
    return NULL;
}

static void begin_time_cb (flux_reactor_t *r,
                           flux_watcher_t *w,
                           int revents,
                           void *arg)
{
    struct begin_time_arg *b = arg;
    flux_t *h = flux_jobtap_get_flux (b->p);
    if (flux_jobtap_dependency_remove (b->p, b->id, b->desc) < 0)
        flux_log_error (h, "begin-time: flux_jobtap_dependency_remove");
    if (flux_jobtap_job_aux_delete_value (b->p, b->id, b) < 0)
        flux_log_error (h, "begin-time: flux_jobtap_job_aux_delete_value");
}


static int add_begin_time (flux_plugin_t *p,
                           flux_t *h,
                           flux_reactor_t *r,
                           flux_jobid_t id,
                           double begin_time)
{
    struct begin_time_arg *arg = NULL;

    if (!(arg = begin_time_arg_create (p, id, begin_time))) {
        flux_log (h, LOG_ERR, "failed to create begin-time args");
        goto error;
    }

    if (!(arg->w = flux_periodic_watcher_create (r,
                                                 begin_time,
                                                 0.,
                                                 NULL,
                                                 begin_time_cb,
                                                 arg))) {
        flux_log_error (h, "flux_periodic_watcher_create");
        goto error;
    }
    flux_watcher_start (arg->w);

    if (flux_jobtap_dependency_add (p, id, arg->desc) < 0) {
        flux_log_error (h, "%s: flux_jobtap_dependency_add", idf58 (id));
        goto error;
    }

    /*  In case job is destroyed before begin_time, tie destruction
     *   of this watcher to the current job.
     */
    if (flux_jobtap_job_aux_set (p,
                                 FLUX_JOBTAP_CURRENT_JOB,
                                 "flux::begin-time",
                                 arg,
                                 (flux_free_f) begin_time_arg_destroy) < 0) {
        flux_log_error (h, "flux_jobtap_job_aux_set");
        goto error;
    }
    return 0;
error:
    begin_time_arg_destroy (arg);
    return -1;
}

/*  Parse string 's' to a floating-point timestamp,
 *   ensuring validity of the result.
 */
static int parse_timestamp (const char *s, double *dp)
{
    double d;
    char *p;
    if (s == NULL) {
        errno = EINVAL;
        return -1;
    }
    d = strtod (s, &p);

    /*  Ensure d is a valid timestamp
     */
    if (d < 0. || isnan(d) || isinf(d) || *p != '\0') {
        errno = EINVAL;
        return -1;
    }
    *dp = d;
    return 0;
}

/*  Handle job.dependency.begin-time requests
 */
static int depend_cb (flux_plugin_t *p,
                      const char *topic,
                      flux_plugin_arg_t *args,
                      void *arg)
{
    flux_jobid_t id;
    const char *s;
    double begin_time = 0.;
    flux_reactor_t *r;
    flux_t *h = flux_jobtap_get_flux (p);

    if (!h || !(r = flux_get_reactor (h)))
        return -1;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I s:{s:s}}",
                                "id", &id,
                                "dependency",
                                  "value", &s) < 0)
        return flux_jobtap_reject_job (p,
                                       args,
                                       "error processing begin-time: %s",
                                       flux_plugin_arg_strerror (args));
    if (parse_timestamp (s, &begin_time) < 0)
        return flux_jobtap_reject_job (p,
                                       args,
                                       "Invalid begin-time=%s",
                                       s);

    if (add_begin_time (p, h, r, id, begin_time) < 0)
        return flux_jobtap_reject_job (p,
                                       args,
                                       "Unable to initialize begin-time");
    return 0;
}

int begin_time_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_add_handler (p,
                                    "job.dependency.begin-time",
                                    depend_cb,
                                    NULL);
}

// vi:ts=4 sw=4 expandtab
