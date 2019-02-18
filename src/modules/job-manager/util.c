/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* util - misc. job manager support
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <ctype.h>
#include <argz.h>
#include <envz.h>

#include <flux/core.h>
#include "src/common/libutil/fluid.h"

#include "job.h"
#include "util.h"

int util_int_from_context (const char *context, const char *key, int *val)
{
    char *argz = NULL;
    size_t argz_len = 0;
    char *endptr;
    unsigned long i;
    char *s;

    if (argz_create_sep (context, ' ', &argz, &argz_len) != 0) {
        errno = ENOMEM;
        return -1;
    }
    if (!(s = envz_get (argz, argz_len, key))) {
        free (argz);
        errno = ENOENT;
        return -1;
    }
    errno = 0;
    i = strtoll (s, &endptr, 10);
    if (errno != 0 || *endptr != '\0' || endptr == s) {
        free (argz);
        errno = EINVAL;
        return -1;
    }
    if (val)
        *val = i;
    free (argz);
    return 0;
}

int util_str_from_context (const char *context, const char *key,
                           char *val, int valsize)
{
    char *argz = NULL;
    size_t argz_len = 0;
    char *s;

    if (argz_create_sep (context, ' ', &argz, &argz_len) != 0) {
        errno = ENOMEM;
        return -1;
    }
    if (!(s = envz_get (argz, argz_len, key))) {
        free (argz);
        errno = ENOENT;
        return -1;
    }
    if (val) {
        if (valsize < strlen (s) + 1) {
            free (argz);
            errno = EINVAL;
            return -1;
        }
        strcpy (val, s);
    }
    free (argz);
    return 0;
}

/* Skip over key=val and any trailing whitespace
 */
static const char *skip_keyval (const char *s)
{
    const char *equal = strchr (s, '=');
    const char *cp = s;

    if (equal) {
        while (cp < equal && !isspace (*cp))
            cp++;
        if (cp++ != equal)
            return NULL;
        while (*cp && !isspace (*cp))
            cp++;
        while (*cp && isspace (*cp))
            cp++;
        return cp;
    }
    return NULL;
}

const char *util_note_from_context (const char *context)
{
    const char *p;

    if (context) {
        while ((p = skip_keyval (context)))
            context = p;
        if (strlen (context) == 0)
            context = NULL;
    }
    return context;
}

int util_jobkey (char *buf, int bufsz, bool active,
                 flux_jobid_t id, const char *key)
{
    char idstr[32];
    int len;

    if (fluid_encode (idstr, sizeof (idstr), id, FLUID_STRING_DOTHEX) < 0)
        return -1;
    len = snprintf (buf, bufsz, "job.%s.%s%s%s",
                    active ? "active" : "inactive",
                    idstr,
                    key ? "." : "",
                    key ? key : "");
    if (len >= bufsz)
        return -1;
    return len;
}

int util_eventlog_append (flux_kvs_txn_t *txn,
                          flux_jobid_t id,
                          const char *name,
                          const char *fmt, ...)
{
    va_list ap;
    char context[FLUX_KVS_MAX_EVENT_CONTEXT + 1];
    int n;
    char path[64];
    char *event = NULL;
    int saved_errno;

    va_start (ap, fmt);
    n = vsnprintf (context, sizeof (context), fmt, ap);
    va_end (ap);
    if (n >= sizeof (context))
        goto error_inval;
    if (util_jobkey (path, sizeof (path), true, id, "eventlog") < 0)
        goto error_inval;
    if (!(event = flux_kvs_event_encode (name, context)))
        goto error;
    if (flux_kvs_txn_put (txn, FLUX_KVS_APPEND, path, event) < 0)
        goto error;
    free (event);
    return 0;
error_inval:
    errno = EINVAL;
error:
    saved_errno = errno;
    free (event);
    errno = saved_errno;
    return -1;
}

int util_attr_pack (flux_kvs_txn_t *txn,
                    flux_jobid_t id,
                    const char *key,
                    const char *fmt, ...)
{
    va_list ap;
    int n;
    char path[64];

    if (util_jobkey (path, sizeof (path), true, id, key) < 0)
        goto error_inval;
    va_start (ap, fmt);
    n = flux_kvs_txn_vpack (txn, 0, path, fmt, ap);
    va_end (ap);
    if (n < 0)
        goto error;
    return 0;
error_inval:
    errno = EINVAL;
error:
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
