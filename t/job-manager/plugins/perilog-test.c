/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* perilog-test.c - basic tests for job manager prolog/epilog
 */

#include <errno.h>
#include <string.h>
#include <jansson.h>

#include <flux/core.h>
#include <flux/jobtap.h>
#include "ccan/str/str.h"

struct perilog_data {
    flux_plugin_t *p;
    flux_jobid_t id;
    char *name;
    bool prolog;
    int status;
};

static int prolog_exception = 0;
static int prolog_count = 1;

static struct perilog_data *
perilog_data_create (flux_plugin_t *p,
                     flux_jobid_t id,
                     bool prolog,
                     const char *name,
                     int status)
{
    struct perilog_data *d = malloc (sizeof (*d));
    if (!d)
        return NULL;
    if (!(d->name = strdup (name))) {
        free (d);
        return NULL;
    }
    d->p = p;
    d->id = id;
    d->prolog = prolog;
    d->status = status;
    return d;
}

static void perilog_data_destroy (struct perilog_data *d)
{
    if (d) {
        free (d->name);
        free (d);
    }
}

static void timer_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents, void *arg)
{
    struct perilog_data *d = arg;
    if (d->prolog) {
        if (flux_jobtap_prolog_finish (d->p, d->id, d->name, d->status) < 0)
            flux_jobtap_raise_exception (d->p, FLUX_JOBTAP_CURRENT_JOB,
                                        "test", 0,
                                        "flux_jobtap_prolog_finish: %s",
                                        strerror (errno));
    }
    else {
        if (flux_jobtap_epilog_finish (d->p, d->id, d->name, d->status) < 0)
            flux_jobtap_raise_exception (d->p, FLUX_JOBTAP_CURRENT_JOB,
                                        "test", 0,
                                        "flux_jobtap_epilog_finish: %s",
                                        strerror (errno));
    }
    flux_watcher_destroy (w);
    perilog_data_destroy (d);
}

static int cb (flux_plugin_t *p,
               const char *topic,
               flux_plugin_arg_t *args,
               void *arg)
{
    flux_t *h = flux_jobtap_get_flux (p);
    flux_watcher_t *tw;
    flux_jobid_t id;
    struct perilog_data *d;
    int rc;
    int prolog = streq (topic, "job.state.run");

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I}",
                                "id", &id) < 0) {
        flux_log_error (h, "flux_plugin_arg_unpack");
        return -1;
    }

    if (!(d = perilog_data_create (p, id, prolog, "test", 0))) {
        flux_log_error (h, "perilog_data_create");
        return -1;
    }

    tw = flux_timer_watcher_create (flux_get_reactor (h),
                                    0.1,
                                    0.0,
                                    timer_cb,
                                    d);
    if (tw == NULL) {
        flux_log_error (h, "timer_watcher_create");
        return -1;
    }

    flux_watcher_start (tw);
    if (prolog) {
        int count = prolog_count;
        rc = flux_jobtap_prolog_start (p, "test");
        while (--count) {
            char name[64];
            (void) snprintf (name, sizeof (name), "test-%d", prolog_count);
            if (!(d = perilog_data_create (p, id, prolog, name, 0))) {
                flux_log_error (h, "perilog_data_create");
                return -1;
            }

            tw = flux_timer_watcher_create (flux_get_reactor (h),
                                            0.1,
                                            0.0,
                                            timer_cb,
                                            d);
            if (tw == NULL) {
                flux_log_error (h, "timer_watcher_create");
                return -1;
            }
            flux_watcher_start (tw);
            rc = flux_jobtap_prolog_start (p, name);
        }
    }
    else
        rc = flux_jobtap_epilog_start (p, "test");
    if (rc < 0) {
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "test", 0,
                                     "flux_jobtap_%s_start failed: %s",
                                     prolog ? "prolog" : "epilog",
                                     strerror (errno));
    }
    if (prolog && prolog_exception) {
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "test", 0,
                                     "prolog test exception");
        /* Use timer_cb to finish prolog */
        timer_cb (flux_get_reactor (h), tw, 0, d);
    }
    return 0;
}

static const struct flux_plugin_handler tab[] = {
    { "job.state.run",     cb, NULL },
    { "job.state.cleanup", cb, NULL },
    { 0 },
};

int flux_plugin_init (flux_plugin_t *p)
{
    if (flux_plugin_register (p, "perilog-test", tab) < 0)
        return -1;
    flux_plugin_conf_unpack (p, "{s?i s?i}",
                             "prolog-exception", &prolog_exception,
                             "prolog-count", &prolog_count);
    return 0;
}

// vi:ts=4 sw=4 expandtab
