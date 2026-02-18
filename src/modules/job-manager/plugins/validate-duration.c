/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/*  validate-duration.c - Ensure a job's duration doesn't exceed
 *   the current resource expiration (at the moment of submission)
 */
#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <math.h>
#include <flux/core.h>
#include <flux/jobtap.h>

#include "src/common/libutil/fsd.h"

static double expiration = 0.;

static int job_duration_check (flux_plugin_t *p,
                               flux_plugin_arg_t *args,
                               double duration)
{
    flux_t *h = flux_jobtap_get_flux (p);

    if (h && duration > 0. && expiration > 0.) {
        flux_reactor_t *r = flux_get_reactor (h);
        double now = r ? flux_reactor_now (r) : flux_reactor_time ();
        double rem = expiration - now;

        if (duration > rem) {
            char dfsd [64];
            char rfsd [64];

            if (fsd_format_duration_ex (dfsd, sizeof (dfsd), duration, 2) < 0
                || fsd_format_duration_ex (rfsd, sizeof (rfsd), rem, 2) < 0) {
                const char *msg = "duration exceeds instance lifetime";
                return flux_jobtap_reject_job (p, args, "%s", msg);
            }
            return flux_jobtap_reject_job (p,
                                           args,
                                           "job duration (%s) exceeds "
                                           "remaining instance lifetime (%s)",
                                           dfsd,
                                           rfsd);
        }
    }
    return 0;
}

static int validate_duration (flux_plugin_t *p,
                              const char *topic,
                              flux_plugin_arg_t *args,
                              void *arg)
{
    double duration = -1.;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:{s:{s?{s?F}}}}",
                                "jobspec",
                                 "attributes",
                                  "system",
                                   "duration", &duration) < 0) {
        return flux_jobtap_reject_job (p,
                                       args,
                                       "failed to unpack duration: %s",
                                       flux_plugin_arg_strerror (args));
    }
    return job_duration_check (p, args, duration);
}

static void kvs_lookup_cb (flux_future_t *f, void *arg)
{
    flux_t *h = flux_future_get_flux (f);
    double val = expiration;
    if (flux_kvs_lookup_get_unpack (f,
                                    "{s:{s:F}}",
                                    "execution",
                                      "expiration", &val) < 0) {
        flux_log_error (h, "Failed to extract expiration from R update");
    }
    flux_future_reset (f);
    if (fabs (val - expiration) < 1.e-5)
        return;
    expiration = val;
    flux_log (h,
              LOG_DEBUG,
              "duration-validator: updated expiration to %.2f",
              expiration);
}

int validate_duration_plugin_init (flux_plugin_t *p)
{
    flux_t *h;
    flux_future_t *f;

    flux_plugin_set_name (p, ".validate-duration");

    h = flux_jobtap_get_flux (p);

    if (!(f = flux_kvs_lookup (h,
                               NULL,
                               FLUX_KVS_WATCH | FLUX_KVS_WAITCREATE,
                               "resource.R"))) {
        flux_log_error (h, "flux_kvs_lookup");
        return -1;
    }
    flux_plugin_aux_set (p, NULL, f, (flux_free_f) flux_future_destroy);
    if (flux_future_then (f, -1., kvs_lookup_cb, NULL) < 0) {
        flux_log_error (h, "flux_kvs_lookup_get_unpack");
        return -1;
    }
    return flux_plugin_add_handler (p,
                                    "job.validate",
                                    validate_duration,
                                    NULL);
}

// vi:ts=4 sw=4 expandtab
