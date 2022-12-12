/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* upmi.c - universal pmi client for internal use */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <assert.h>

#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"
#include "src/common/libutil/errprintf.h"

#include "upmi.h"
#include "upmi_plugin.h"

struct upmi {
    const struct upmi_plugin *plugin;
    void *ctx;
    int flags;
    char *scheme;
    upmi_trace_f trace_fun;
    void *trace_arg;
};

extern struct upmi_plugin upmi_simple;
extern struct upmi_plugin upmi_libpmi;
extern struct upmi_plugin upmi_pmix;
extern struct upmi_plugin upmi_single;

static const struct upmi_plugin *plugins[] = {
    &upmi_simple,
#ifdef HAVE_LIBPMIX
    &upmi_pmix,
#endif
    &upmi_libpmi,
    &upmi_single,
};

static const struct upmi_plugin *lookup_plugin (const char *scheme)
{
    for (int i = 0; i < ARRAY_SIZE (plugins); i++) {
        if (streq (scheme, plugins[i]->getname ()))
            return plugins[i];
    }
    errno = ENOENT;
    return NULL;
}

void upmi_destroy (struct upmi *pmi)
{
    if (pmi) {
        int saved_errno = errno;
        if (pmi->plugin && pmi->plugin->destroy)
            pmi->plugin->destroy (pmi->ctx);
        free (pmi->scheme);
        free (pmi);
        errno = saved_errno;
    }
}

static struct upmi *upmi_create_one (const char *uri,
                                     int flags,
                                     upmi_trace_f trace_fun,
                                     void *trace_arg,
                                     flux_error_t *error)
{
    struct upmi *upmi;
    char *path;

    if (flags & ~(UPMI_TRACE | UPMI_LIBPMI_NOFLUX)) {
        errprintf (error, "invalid argument");
        return NULL;
    }
    if (!(upmi = calloc (1, sizeof (*upmi)))
        || !(upmi->scheme = strdup (uri))) {
        errprintf (error, "out of memory");
        goto error;
    }
    upmi->flags = flags;
    upmi->trace_fun = trace_fun;
    upmi->trace_arg = trace_arg;
    if ((path = strchr (upmi->scheme, ':')))
        *path++ = '\0';
    if (!(upmi->plugin = lookup_plugin (upmi->scheme))) {
        errprintf (error, "plugin '%s' not found", upmi->scheme);
        goto error;
    }
    if (!(upmi->ctx = upmi->plugin->create (upmi, path, error)))
        goto error;
    return upmi;
error:
    upmi_destroy (upmi);
    return NULL;
}

struct upmi *upmi_create (const char *uri,
                          int flags,
                          upmi_trace_f trace_fun,
                          void *trace_arg,
                          flux_error_t *error)
{
    struct upmi *upmi;

    if (uri) {
        return upmi_create_one (uri,
                                flags,
                                trace_fun,
                                trace_arg,
                                error);
    }
    else {
        for (int i = 0; i < ARRAY_SIZE (plugins); i++) {
            upmi = upmi_create_one (plugins[i]->getname (),
                                    flags,
                                    trace_fun,
                                    trace_arg,
                                    error);
            if (upmi)
                return upmi;
        }
    }
    return NULL;
}

const char *upmi_describe (struct upmi *upmi)
{
    return upmi->scheme;
}

int upmi_initialize (struct upmi *upmi,
                     struct upmi_info *infop,
                     flux_error_t *errp)
{
    struct upmi_info info;
    flux_error_t error;

    if (!upmi) {
        errprintf (errp, "invalid argument");
        return -1;
    }
    if (upmi->plugin->initialize (upmi->ctx, &info, &error) < 0) {
        upmi_trace (upmi, "initialize: %s", error.text);
        errprintf (errp, "%s", error.text);
        return -1;
    }
    upmi_trace (upmi,
                "initialize: rank=%d size=%d name=%s: success",
                info.rank,
                info.size,
                info.name);
    if (infop)
        *infop = info;
    return 0;
}

int upmi_finalize (struct upmi *upmi, flux_error_t *errp)
{
    flux_error_t error;

    if (!upmi) {
        errprintf (errp, "invalid argument");
        return -1;
    }
    if (upmi->plugin->finalize (upmi->ctx, &error) < 0) {
        upmi_trace (upmi, "finalize: %s", error.text);
        errprintf (errp, "%s", error.text);
        return -1;
    }
    upmi_trace (upmi, "finalize: successs");
    return 0;
}

int upmi_put (struct upmi *upmi,
              const char *key,
              const char *value,
              flux_error_t *errp)
{
    flux_error_t error;

    if (!upmi || !key || !value) {
        errprintf (errp, "invalid argument");
        return -1;
    }
    if (upmi->plugin->put (upmi->ctx, key, value, &error) < 0) {
        upmi_trace (upmi, "put key=%s value=%s: %s", key, value, error.text);
        errprintf (errp, "%s", error.text);
        return -1;
    }
    upmi_trace (upmi, "put key=%s value=%s: success", key, value);
    return 0;
}

int upmi_get (struct upmi *upmi,
              const char *key,
              int rank,
              char **valuep,
              flux_error_t *errp)
{
    flux_error_t error;
    char *value = NULL;

    if (!upmi || !key || !valuep) {
        errprintf (errp, "invalid argument");
        return -1;
    }
    if (upmi->plugin->get (upmi->ctx, key, rank, &value, &error) < 0) {
        upmi_trace (upmi, "get key=%s: %s", key, error.text);
        errprintf (errp, "%s", error.text);
        return -1;
    }
    upmi_trace (upmi, "get key=%s value=%s: success", key, value);
    *valuep = value;
    return 0;
}

int upmi_barrier (struct upmi *upmi, flux_error_t *errp)
{
    flux_error_t error;

    if (!upmi) {
        errprintf (errp, "invalid argument");
        return -1;
    }
    if (upmi->plugin->barrier (upmi->ctx, &error) < 0) {
        upmi_trace (upmi, "barrier: %s", error.text);
        errprintf (errp, "%s", error.text);
        return -1;
    }
    upmi_trace (upmi, "barrier: success");
    return 0;
}

static void upmi_vtrace (struct upmi *upmi, const char *fmt, va_list ap)
{
    if ((upmi->flags & UPMI_TRACE) && upmi->trace_fun) {
        char buf[1024];
        snprintf (buf, sizeof (buf), "%s: ", upmi->scheme);
        size_t len = strlen (buf);
        vsnprintf (buf + len, sizeof (buf) - len, fmt, ap);
        upmi->trace_fun (upmi->trace_arg, buf);
    }
}

void upmi_trace (struct upmi *upmi, const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    upmi_vtrace (upmi, fmt, ap);
    va_end (ap);
}

bool upmi_has_flag (struct upmi *upmi, int flag)
{
    return (upmi->flags & flag) ? true : false;
}

// vi:ts=4 sw=4 expandtab
