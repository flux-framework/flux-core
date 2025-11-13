/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* limit-job-size.c - validate job requests against configured job size limits
 *
 * This plugin uses the job.validate callback to accept or reject job
 * requests.  Any default jobspec values would have been applied earlier
 * (where applicable) in the job.create callback.
 *
 * General limit:
 *  [policy.limits.job-size]
 * Queue-specific limit:
 *  [queues.<name>.policy.limits.job-size]
 *
 * N.B. a queue limit may override the general limit with a higher or lower
 * limit, even "unlimited".  Since 0 may be a valid size limit, -1 is reserved
 * to mean unlimited in this situation.
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

#include "src/common/libjob/jj.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "ccan/str/str.h"

#define SIZE_INVALID   (-2)
#define SIZE_UNLIMITED (-1)

#define SIZE_IS_OUT_OF_BOUNDS(n) \
    ((n) < 0 && (n) != SIZE_INVALID && (n) != SIZE_UNLIMITED)

#define LIMIT_OVER(limit,val) \
    ((limit) != SIZE_INVALID && (limit) != SIZE_UNLIMITED && (val) > (limit))
#define LIMIT_UNDER(limit,val) \
    ((limit) != SIZE_INVALID && (limit) != SIZE_UNLIMITED && (val) < (limit))

struct job_size {
    int nnodes;
    int ncores;
    int ngpus;
};

struct limits {
    struct job_size max;
    struct job_size min;
};

struct limit_job_size {
    struct limits general_limits;
    zhashx_t *queues; // queue name => struct limits
    flux_t *h;
};

const char *auxkey = "limit-job-size";

static void job_size_clear (struct job_size *js)
{
    js->nnodes = SIZE_INVALID;
    js->ncores = SIZE_INVALID;
    js->ngpus = SIZE_INVALID;
}

static bool job_size_isset (struct job_size *js)
{
    if (js) {
        if (js->nnodes != SIZE_INVALID
            || js->ncores != SIZE_INVALID
            || js->ngpus != SIZE_INVALID)
            return true;
    }
    return false;
}

static void job_size_override (struct job_size *js1,
                               struct job_size *js2)
{
    if (js1 && js2) {
        if (js2->nnodes != SIZE_INVALID)
            js1->nnodes = js2->nnodes;
        if (js2->ncores != SIZE_INVALID)
            js1->ncores = js2->ncores;
        if (js2->ngpus != SIZE_INVALID)
            js1->ngpus = js2->ngpus;
    }
}

static void limits_clear (struct limits *l)
{
    job_size_clear (&l->max);
    job_size_clear (&l->min);
}

static bool limits_isset (struct limits *l)
{
    if (l) {
        if (job_size_isset (&l->max)
            || job_size_isset (&l->min))
            return true;
    }
    return false;
}

static void limits_override (struct limits *l1,
                             struct limits *l2)
{
    if (l1 && l2) {
        job_size_override (&l1->max, &l2->max);
        job_size_override (&l1->min, &l2->min);
    }
}

// zhashx_destructor_fn footprint
static void limits_destroy (void **item)
{
    if (item) {
        free (*item);
        *item = NULL;
    }
}

// zhashx_duplicator_fn footprint
static void *limits_duplicate (const void *item)
{
    struct limits *limits;

    if (!(limits = calloc (1, sizeof (*limits))))
        return NULL;
    *limits = *(struct limits *)item;
    return limits;
}

static zhashx_t *queues_create (void)
{
    zhashx_t *queues;

    if (!(queues = zhashx_new ())) {
        errno = ENOMEM;
        return NULL;
    }
    zhashx_set_destructor (queues, limits_destroy);
    zhashx_set_duplicator (queues, limits_duplicate);
    return queues;
}

static void queues_insert (zhashx_t *queues,
                           const char *name,
                           struct limits *limits)
{
    (void)zhashx_insert (queues, name, limits); // dups limits
}

static struct limits *queues_lookup (zhashx_t *queues, const char *name)
{
    return queues ? zhashx_lookup (queues, name) : NULL;
}

static void limit_job_size_destroy (struct limit_job_size *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        zhashx_destroy (&ctx->queues);
        free (ctx);
        errno = saved_errno;
    }
}

static struct limit_job_size *limit_job_size_create (flux_t *h)
{
    struct limit_job_size *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    if (!(ctx->queues = queues_create ()))
        goto error;
    ctx->h = h;
    limits_clear (&ctx->general_limits);
    return ctx;
error:
    limit_job_size_destroy (ctx);
    return NULL;
}

static int job_size_parse (struct job_size *jsp,
                           json_t *o,
                           flux_error_t *error)
{
    struct job_size js;

    job_size_clear (&js);
    if (o) {
        json_error_t jerror;

        if (json_unpack_ex (o,
                            &jerror,
                            0,
                            "{s?i s?i s?i}",
                            "nnodes", &js.nnodes,
                            "ncores", &js.ncores,
                            "ngpus", &js.ngpus) < 0) {
            errprintf (error, "%s", jerror.text);
            errno = EINVAL;
            return -1;
        }
        if (SIZE_IS_OUT_OF_BOUNDS (js.nnodes)
            || SIZE_IS_OUT_OF_BOUNDS (js.ncores)
            || SIZE_IS_OUT_OF_BOUNDS (js.ngpus)) {
            errprintf (error, "size must be -1 (unlimited), or >= 0");
            errno = EINVAL;
            return -1;
        }
    }
    *jsp = js;
    return 0;
}

static int limits_parse (struct limits *limitsp,
                         json_t *conf,
                         flux_error_t *error)
{
    struct limits limits;
    json_t *min = NULL;
    json_t *max = NULL;
    json_error_t jerror;
    flux_error_t e;

    if (json_unpack_ex (conf,
                        &jerror,
                        0,
                        "{s?{s?{s?{s?o s?o}}}}",
                        "policy",
                          "limits",
                            "job-size",
                              "max", &max,
                              "min", &min) < 0) {
        errprintf (error, "policy.limits.job-size: %s", jerror.text);
        return -1;
    }
    if (job_size_parse (&limits.max, max, &e) < 0) {
        errprintf (error, "policy.limits.job-size.max: %s", e.text);
        return -1;
    }
    if (job_size_parse (&limits.min, min, &e) < 0) {
        errprintf (error, "policy.limits.job-size.min: %s", e.text);
        return -1;
    }
    *limitsp = limits;
    return 0;
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
        struct limits limits;
        flux_error_t e;

        json_object_foreach (queues, name, entry) {
            if (limits_parse (&limits, entry, &e) < 0) {
                errprintf (error, "queues.%s.%s", name, e.text);
                goto error;
            }
            queues_insert (zh, name, &limits);
        }
    }
    *zhp = zh;
    return 0;
error:
    ERRNO_SAFE_WRAP (zhashx_destroy, &zh);
    return -1;
}

static int check_limit (const char *queue,
                        const char *resource,
                        bool over,
                        int limit,
                        int value,
                        flux_error_t *errp)
{
    if ((over && LIMIT_OVER (limit, value))
        || (!over && LIMIT_UNDER (limit, value)))
        return errprintf (errp,
                          "requested %s (%d) %s policy limit of %d%s%s",
                          resource,
                          value,
                          over ? "exceeds" : "is under",
                          limit,
                          queue ? " for queue ": "",
                          queue ? queue : "");
    return 0;
}

static int check_over (const char *queue,
                       const char *resource,
                       int limit,
                       int value,
                       flux_error_t *errp)
{
    return check_limit (queue, resource, true, limit, value, errp);
}

static int check_under (const char *queue,
                        const char *resource,
                        int limit,
                        int value,
                        flux_error_t *errp)
{
    return check_limit (queue, resource, false, limit, value, errp);
}


static int check_limits (struct limit_job_size *ctx,
                         struct jj_counts *counts,
                         const char *queue,
                         flux_error_t *error)
{
    struct limits limits;
    struct limits *queue_limits;

    int nnodes = counts->nnodes;
    int ncores = counts->nslots * counts->slot_size;
    int ngpus = counts->nslots * counts->slot_gpus;

    limits = ctx->general_limits;
    if (queue && (queue_limits = queues_lookup (ctx->queues, queue)))
        limits_override (&limits, queue_limits);

    if (check_over (queue, "nnodes", limits.max.nnodes, nnodes, error) < 0
        || check_over (queue, "ncores", limits.max.ncores, ncores, error) < 0
        || check_over (queue, "ngpus", limits.max.ngpus, ngpus, error) < 0
        || check_under (queue, "nnodes", limits.min.nnodes, nnodes, error) < 0
        || check_under (queue, "ncores", limits.min.ncores, ncores, error) < 0
        || check_under (queue, "ngpus", limits.min.ngpus, ngpus, error) < 0)
        return -1;
    return 0;
}

static int validate_cb (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *arg)
{
    struct limit_job_size *ctx = flux_plugin_aux_get (p, auxkey);
    flux_job_state_t state;
    json_t *jobspec = NULL;
    struct jj_counts counts;
    const char *queue = NULL;
    flux_error_t error;
    json_error_t jerror;

    /* If no limits are configured, return immediately.  This is the common
     * case for a non-system instance and since this plugin is always loaded,
     * don't waste time.
     */
    if (!limits_isset (&ctx->general_limits)
        && zhashx_size (ctx->queues) == 0)
        return 0;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:i s:o}",
                                "state", &state,
                                "jobspec", &jobspec) < 0) {
        errprintf (&error,
                   "limit-job-size: error unpacking job.validate arguments: %s",
                   flux_plugin_arg_strerror (args));
        goto error;
    }

    if (jj_get_counts_json (jobspec, &counts) < 0) {
        errprintf (&error, "%s", counts.error);
        goto error;
    }

    /* Parse (optional) jobspec attributes.system.queue.
     * Leave queue NULL if unspecified.
     * Throw an error if it's the wrong type or related.
     */
    if (json_unpack_ex (jobspec,
                        &jerror,
                        0,
                        "{s:{s?{s?s}}}",
                        "attributes",
                          "system",
                            "queue", &queue) < 0) {
        errprintf (&error,
                   "Error parsing jobspec attributes.system.queue: %s",
                   jerror.text);
        goto error;
    }

    if (check_limits (ctx, &counts, queue, &error) < 0)
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
    struct limit_job_size *ctx = flux_plugin_aux_get (p, auxkey);
    flux_error_t error;
    json_t *conf;
    struct limits limits;
    zhashx_t *queues;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:o}",
                                "conf", &conf) < 0) {
        errprintf (&error,
                   "limit-job-size: error unpacking conf.update arguments: %s",
                   flux_plugin_arg_strerror (args));
        goto error;
    }
    if (limits_parse (&limits, conf, &error) < 0)
        goto error;
    if (queues_parse (&queues, conf, &error) < 0)
        goto error;
    ctx->general_limits = limits;
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

int limit_job_size_plugin_init (flux_plugin_t *p)
{
    struct limit_job_size *ctx;

    if (!(ctx = limit_job_size_create (flux_jobtap_get_flux (p)))
        || flux_plugin_aux_set (p,
                                auxkey,
                                ctx,
                                (flux_free_f)limit_job_size_destroy) < 0) {
        limit_job_size_destroy (ctx);
        return -1;
    }

    return flux_plugin_register (p, ".limit-job-size", tab);
}

// vi:ts=4 sw=4 expandtab
