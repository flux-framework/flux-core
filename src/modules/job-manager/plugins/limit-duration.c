/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* limit-duration.c - validate job requests against configured duration limits
 *
 * This plugin uses the job.validate callback to accept or reject job
 * requests.  Any default jobspec values would have been applied earlier
 * (where applicable) at ingest.
 *
 * General limit:
 *  policy.limits.duration
 * Queue-specific limit:
 *  queues.<name>.policy.limits.duration
 *
 * N.B. a queue limit may override the general limit with a higher or lower
 * limit, or "0" for unlimited.
 *
 * See also:
 *  RFC 33/Flux Job Queues
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>
#include <flux/jobtap.h>

#include "src/common/libutil/fsd.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "ccan/str/str.h"

struct limit_duration {
    double general_limit;   // general duration limit (seconds)
    zhashx_t *queues;       // queue name => duration limit (double * seconds)
    flux_t *h;
};

#define DURATION_INVALID      (-1)
#define DURATION_UNLIMITED    (0)

static const char *auxkey = "limit-duration";

// zhashx_destructor_fn footprint
static void duration_destroy (void **item)
{
    if (item) {
        free (*item);
        *item = NULL;
    }
}

// zhashx_duplicator_fn footprint
static void *duration_duplicate (const void *item)
{
    double *cpy;
    if (!(cpy = calloc (1, sizeof (*cpy))))
        return NULL;
    *cpy = *(double *)item;
    return cpy;
}

static zhashx_t *queues_create (void)
{
    zhashx_t *queues;

    if (!(queues = zhashx_new ())) {
        errno = ENOMEM;
        return NULL;
    }
    zhashx_set_destructor (queues, duration_destroy);
    zhashx_set_duplicator (queues, duration_duplicate);
    return queues;
}

static double queues_lookup (zhashx_t *queues, const char *name)
{
    double *dp;

    if (name && (dp = zhashx_lookup (queues, name)))
        return *dp;
    return DURATION_INVALID;
}

static void queues_insert (zhashx_t *queues, const char *name, double duration)
{
    (void)zhashx_insert (queues, name, &duration); // dups duration
}

static void limit_duration_destroy (struct limit_duration *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        zhashx_destroy (&ctx->queues);
        free (ctx);
        errno = saved_errno;
    }
}

static struct limit_duration *limit_duration_create (flux_t *h)
{
    struct limit_duration *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    if (!(ctx->queues = queues_create ()))
        goto error;
    ctx->h = h;
    ctx->general_limit = DURATION_INVALID;
    return ctx;
error:
    limit_duration_destroy (ctx);
    return NULL;
}

static int duration_parse (double *duration,
                           json_t *conf,
                           flux_error_t *error)
{
    double d = DURATION_INVALID;
    const char *ds = NULL;
    json_error_t jerror;
    const char *name = "policy.limits.duration";

    if (json_unpack_ex (conf,
                        &jerror,
                        0,
                        "{s?{s?{s?s}}}",
                        "policy",
                          "limits",
                            "duration", &ds) < 0) {
        errprintf (error, "%s: %s", name, jerror.text);
        goto inval;
    }
    if (ds) {
        if (fsd_parse_duration (ds, &d) < 0) {
            errprintf (error, "%s: FSD value is malformed", name);
            return -1;
        }
    }
    *duration = d;
    return 0;
inval:
    errno = EINVAL;
    return -1;
}

static int queues_parse (zhashx_t **zhp,
                         json_t *conf,
                         flux_error_t *error)
{
    json_t *queues;
    zhashx_t *zh;

    if (!(zh = queues_create ())) {
        errprintf (error, "out of memory parsing [queues]");
        goto error;
    }
    if ((queues = json_object_get (conf, "queues"))) {
        const char *name;
        json_t *entry;
        double duration;
        flux_error_t e;

        json_object_foreach (queues, name, entry) {
            if (duration_parse (&duration, entry, &e) < 0) {
                errprintf (error, "queues.%s.%s", name, e.text);
                goto error;
            }
            queues_insert (zh, name, duration);
        }
    }
    *zhp = zh;
    return 0;
error:
    ERRNO_SAFE_WRAP (zhashx_destroy, &zh);
    return -1;
}

static int check_limit (struct limit_duration *ctx,
                        double duration,
                        const char *queue,
                        flux_error_t *error)
{
    double limit = ctx->general_limit;
    double qlimit = queues_lookup (ctx->queues, queue);
    bool unlimited = duration == DURATION_UNLIMITED;

    if (qlimit != DURATION_INVALID)
        limit = qlimit;
    if (limit != DURATION_INVALID
        && limit != DURATION_UNLIMITED
        && (duration > limit || unlimited)) {
        char requested[64];
        char fsd[64];
        fsd_format_duration_ex (requested, sizeof (requested), duration, 2);
        fsd_format_duration_ex (fsd, sizeof (fsd), limit, 2);
        return errprintf (error,
                          "requested duration (%s) exceeds policy limit of "
                          "%s%s%s",
                          unlimited ? "unlimited" : requested,
                          fsd,
                          queue ? " for queue " : "",
                          queue ? queue : "");
    }
    return 0;
}

static int validate_cb (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *arg)
{
    struct limit_duration *ctx = flux_plugin_aux_get (p, auxkey);
    flux_job_state_t state;
    double duration = DURATION_UNLIMITED;
    const char *queue = NULL;
    flux_error_t error;

    /* If no limits are configured, return immediately.  This is the common
     * case for a non-system instance and since this plugin is always loaded,
     * don't waste time.
     */
    if ((ctx->general_limit == DURATION_INVALID
        || ctx->general_limit == DURATION_UNLIMITED)
        && zhashx_size (ctx->queues) == 0)
        return 0;

    /* Parse jobspec attributes:
     * - attributes.system.queue (NULL if unspecified)
     * - attributes.system.duration (DURATION_UNLIMITED if unspecified)
     */
    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:i s?{s?{s?{s?F s?s}}}}",
                                "state", &state,
                                "jobspec",
                                  "attributes",
                                    "system",
                                      "duration", &duration,
                                      "queue", &queue) < 0) {
        errprintf (&error,
                   "limit-duration: error unpacking job.validate arguments: %s",
                   flux_plugin_arg_strerror (args));
        goto error;
    }

    if (check_limit (ctx, duration, queue, &error) < 0)
        goto error;

    return 0;
error:
    flux_jobtap_reject_job (p, args, "%s", error.text);
    return -1;
}

/* conf.update callback - called on plugin load, and when config is updated
 * This function has two purposes:
 * - Validate proposed 'conf' and return human readable errors if rejected
 * - Pre-parse and cache the config in 'ctx' to streamline job validation
 */
static int conf_update_cb (flux_plugin_t *p,
                           const char *topic,
                           flux_plugin_arg_t *args,
                           void *arg)
{
    struct limit_duration *ctx = flux_plugin_aux_get (p, auxkey);
    flux_error_t error;
    json_t *conf;
    double duration;
    zhashx_t *queues;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:o}",
                                "conf", &conf) < 0) {
        errprintf (&error,
                   "limit-duration: error unpacking conf.update arguments: %s",
                   flux_plugin_arg_strerror (args));
        goto error;
    }
    if (duration_parse (&duration, conf, &error) < 0
        || queues_parse (&queues, conf, &error) < 0)
        goto error;
    ctx->general_limit = duration;
    zhashx_destroy (&ctx->queues);
    ctx->queues = queues;
    return 0;
error:
    return flux_jobtap_error (p, args, "%s", error.text);
}

static const struct flux_plugin_handler tab[] = {
    { "job.validate", validate_cb, NULL },
    { "conf.update", conf_update_cb, NULL },
    { 0 }
};

int limit_duration_plugin_init (flux_plugin_t *p)
{
    struct limit_duration *ctx;

    if (!(ctx = limit_duration_create (flux_jobtap_get_flux (p)))
        || flux_plugin_aux_set (p,
                                auxkey,
                                ctx,
                                (flux_free_f)limit_duration_destroy) < 0) {
        limit_duration_destroy (ctx);
        return -1;
    }

    return flux_plugin_register (p, ".limit-duration", tab);
}

// vi:ts=4 sw=4 expandtab
