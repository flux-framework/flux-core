/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* bootstrap.c - determine rank, size, and peer endpoints */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"

#include "attr.h"
#include "bootstrap.h"

/* Ensure attribute 'key' is set with the immutable flag.
 * If unset, set it to 'default_value'.
 */
static int setattr (attr_t *attrs,
                    const char *key,
                    const char *default_value,
                    flux_error_t *errp)
{
    int flags;
    const char *val;

    if (attr_get (attrs, key, &val, &flags) < 0) {
        if (attr_add (attrs, key, default_value, ATTR_IMMUTABLE) < 0) {
            errprintf (errp, "setattr %s: %s", key, strerror (errno));
            return -1;
        }
    }
    else if (!(flags & ATTR_IMMUTABLE)) {
        if (attr_set_flags (attrs, key, ATTR_IMMUTABLE) < 0) {
            errprintf (errp, "setattr-flags %s: %s", key, strerror (errno));
            return -1;
        }
    }
    return 0;
}

static const char *getattr (attr_t *attrs, const char *key)
{
    const char *val;
    if (attr_get (attrs, key, &val, NULL) < 0)
        return NULL;
    return val;
}

static char *lookup (struct upmi *upmi, const char *key)
{
    char *val;
    if (upmi_get (upmi, key, -1, &val, NULL) < 0)
        return NULL;
    return val;
}

/* Initialize some broker attributes using information obtained during
 * bootstrap, such as pre-put values from the PMI KVS.
 */
static int bootstrap_setattrs_early (struct bootstrap *boot,
                                     flux_error_t *errp)
{
    attr_t *attrs = boot->ctx->attrs;

    /* The info->dict exists so that out of tree upmi plugins, such as the
     * one provided by flux-pmix, can set Flux broker attributes as a way
     * of passing information to applications.
     */
    if (boot->ctx->info.dict) {
        const char *key;
        json_t *value;

        json_object_foreach (boot->ctx->info.dict, key, value) {
            if (!json_is_string (value)) {
                errprintf (errp, "info dict key %s is not a string", key);
                return -1;
            }
            if (setattr (attrs, key, json_string_value (value), errp) < 0)
                return -1;
        }
    }

    /* If running under Flux, setattr instance-level from PMI
     * flux.instance-level.  If not running under Flux (key is missing),
     * set it to zero.
     */
    if (boot->under_flux) {
        char *val = lookup (boot->upmi, "flux.instance-level");
        int rc;

        if (!val)
            boot->under_flux = false;
        rc = setattr (attrs, "instance-level", val ? val : "0", errp);
        free (val);
        if (rc < 0)
            return -1;
    }

    /* If running under Flux, setattr jobid to PMI KVS name.
     */
    if (boot->under_flux) {
        if (setattr (attrs, "jobid", boot->ctx->info.name, errp))
            return -1;
    }

    /* If running under Flux, and not already set, setattr tbon.interface-hint
     * from PMI flux.tbon.interface-hint, if available.
     * This is finalized later by the overlay.
     */
    if (boot->under_flux && !getattr (attrs, "tbon.interface-hint")) {
        char *val = lookup (boot->upmi, "flux.tbon-interface-hint");
        int rc;

        if (val) {
            rc = setattr (attrs, "tbon.interface-hint", val, errp);
            free (val);
            if (rc < 0)
                return -1;
        }
    }

    return 0;
}

const char *bootstrap_method (struct bootstrap *boot)
{
    return boot ? upmi_describe (boot->upmi) : "unknown";
}

static void trace_upmi (void *arg, const char *text)
{
    fprintf (stderr, "bootstrap: %s\n", text);
}

void bootstrap_destroy (struct bootstrap *boot)
{
    if (boot) {
        int saved_errno = errno;
        bizcache_destroy (boot->cache);
        upmi_destroy (boot->upmi);
        free (boot);
        errno = saved_errno;
    }
}

struct bootstrap *bootstrap_create (struct broker *ctx,
                                    struct upmi_info *info,
                                    flux_error_t *errp)
{
    struct bootstrap *boot;
    const char *upmi_method = NULL;
    int upmi_flags = UPMI_LIBPMI_NOFLUX;
    json_t *upmi_args = NULL;
    json_t *conf_obj;
    flux_error_t error;

    if (!(boot = calloc (1, sizeof (*boot)))) {
        errprintf (errp, "out of memory");
        return NULL;
    }
    boot->ctx = ctx;

    if (getenv ("FLUX_PMI_DEBUG"))
        upmi_flags |= UPMI_TRACE;

    if (flux_conf_unpack (flux_get_conf (ctx->h), NULL, "o", &conf_obj) < 0
        || !(upmi_args = json_pack ("{s:O s:s}",
                                    "config", conf_obj,
                                    "hostname", ctx->hostname))) {
        errprintf (errp, "error preparing upmi_args");
        goto error;
    }
    (void)attr_get (ctx->attrs, "broker.boot-method", &upmi_method, NULL);
    if (!(boot->upmi = upmi_create_ex (upmi_method,
                                       upmi_flags,
                                       upmi_args,
                                       trace_upmi,
                                       NULL,
                                       errp)))
        goto error;
    if (setattr (ctx->attrs,
                 "broker.boot-method",
                 upmi_describe (boot->upmi),
                 errp) < 0)
        goto error;

    if (upmi_initialize (boot->upmi, info, &error) < 0) {
        errprintf (errp,
                   "%s: initialize: %s",
                   upmi_describe (boot->upmi),
                   error.text);
        goto error;
    }
    boot->under_flux = true; // until proven otherwise
    if (boot->ctx->verbose) {
        flux_log (boot->ctx->h,
                  LOG_INFO,
                  "boot: rank=%d size=%d",
                  info->rank,
                  info->size);
    }
    if (bootstrap_setattrs_early (boot, &error) < 0) {
        errprintf (errp, "%s: %s", upmi_describe (boot->upmi), error.text);
        goto error;
    }
    if (!(boot->cache = bizcache_create (boot->upmi, info->size))) {
        errprintf (errp,
                   "%s: error creating business card cache: %s",
                   upmi_describe (boot->upmi),
                   strerror (errno));
        goto error;
    }
    json_decref (upmi_args);
    return boot;
error:
    ERRNO_SAFE_WRAP (json_decref, upmi_args);
    bootstrap_destroy (boot);
    return NULL;
}

// vi:ts=4 sw=4 expandtab
