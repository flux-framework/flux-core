/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>
#include <czmq.h>
#include <argz.h>

#include "eventlog.h"

static json_t *eventlog_entry_decode_common (const char *event,
                                             bool trailing_newline);

int eventlog_entry_parse (json_t *entry,
                          double *timestamp,
                          const char **name,
                          json_t **context)
{
    double t;
    const char *n;
    json_t *c;

    if (!entry) {
        errno = EINVAL;
        return -1;
    }

    if (json_unpack (entry, "{ s:F s:s }",
                     "timestamp", &t,
                     "name", &n) < 0) {
        errno = EINVAL;
        return -1;
    }

    if (!json_unpack (entry, "{ s:o }", "context", &c)) {
        if (!json_is_object (c)) {
            errno = EINVAL;
            return -1;
        }
    }
    else
        c = NULL;

    if (timestamp)
        (*timestamp) = t;
    if (name)
        (*name) = n;
    if (context)
        (*context) = c;

    return 0;
}

static int eventlog_entry_append (json_t *a, const char *event)
{
    json_t *o = NULL;

    if (!(o = eventlog_entry_decode_common (event, false)))
        return -1;

    if (json_array_append_new (a, o) < 0) {
        json_decref (o);
        errno = ENOMEM;
        return -1;
    }

    return 0;
}

json_t *eventlog_decode (const char *s)
{
    char *copy = NULL;
    char *input;
    json_t *a = NULL;
    int save_errno;
    char *ptr;

    if (!s) {
        errno = EINVAL;
        return NULL;
    }

    if (!(copy = strdup (s)))
        return NULL;
    input = copy;

    /* gotta have atleast 1 newline, if not empty string */
    if (strlen (s) && !(ptr = strchr (input, '\n'))) {
        errno = EINVAL;
        goto error;
    }

    if (!(a = json_array ())) {
        errno = ENOMEM;
        goto error;
    }

    while ((ptr = strchr (input, '\n'))) {
        (*ptr++) = '\0';

        if (eventlog_entry_append (a, input) < 0)
            goto error;

        input = ptr;
    }

    free (copy);
    return a;

 error:
    save_errno = errno;
    free (copy);
    json_decref (a);
    errno = save_errno;
    return NULL;
}

bool eventlog_entry_validate (json_t *entry)
{
    json_t *name;
    json_t *timestamp;
    json_t *context;

    if (!json_is_object (entry)
        || !(name = json_object_get (entry, "name"))
        || !json_is_string (name)
        || !(timestamp = json_object_get (entry, "timestamp"))
        || !json_is_number (timestamp))
        return false;

    if ((context = json_object_get (entry, "context"))) {
        if (!json_is_object (context))
            return false;
    }

    return true;
}

static json_t *eventlog_entry_decode_common (const char *entry,
                                             bool trailing_newline)
{
    int len;
    char *ptr;
    json_t *o;

    if (!entry)
        goto einval;

    if (!(len = strlen (entry)))
        goto einval;

    if (trailing_newline) {
        if (entry[len - 1] != '\n')
            goto einval;

        ptr = strchr (entry, '\n');
        if (ptr != &entry[len - 1])
            goto einval;
    }
    else {
        if ((ptr = strchr (entry, '\n')))
            goto einval;
    }

    if (!(o = json_loads (entry, 0, NULL)))
        goto einval;

    if (!eventlog_entry_validate (o)) {
        json_decref (o);
        goto einval;
    }

    return o;

 einval:
    errno = EINVAL;
    return NULL;
}

json_t *eventlog_entry_decode (const char *entry)
{
    return eventlog_entry_decode_common (entry, true);
}

static int get_timestamp_now (double *timestamp)
{
    struct timespec ts;
    if (clock_gettime (CLOCK_REALTIME, &ts) < 0)
        return -1;
    *timestamp = (1E-9 * ts.tv_nsec) + ts.tv_sec;
    return 0;
}

static json_t *entry_build (double timestamp,
                            const char *name,
                            json_t *context)
{
    json_t *o = NULL;

    if (timestamp < 0.
        || !name
        || !strlen (name)
        || (context && !json_is_object (context))) {
        errno = EINVAL;
        return NULL;
    }
    if (timestamp == 0.) {
        if (get_timestamp_now (&timestamp) < 0)
            return NULL;
    }
    if (!context)
        o = json_pack ("{ s:f s:s }",
                       "timestamp", timestamp,
                       "name", name);
    else
        o = json_pack ("{ s:f s:s s:O }",
                       "timestamp", timestamp,
                       "name", name,
                       "context", context);
    if (!o) {
        errno = ENOMEM;
        return NULL;
    }
    return o;
}

json_t *eventlog_entry_create (double timestamp, const char *name,
                               const char *context)
{
    json_t *rv = NULL;
    json_t *c = NULL;
    int save_errno;

    if (context) {
        if (!(c = json_loads (context, 0, NULL))) {
            errno = EINVAL;
            goto error;
        }
    }
    rv = entry_build (timestamp, name, c);
 error:
    save_errno = errno;
    json_decref (c);
    errno = save_errno;
    return rv;
}

json_t *eventlog_entry_vpack (double timestamp,
                              const char *name,
                              const char *context_fmt,
                              va_list ap)
{
    json_t *rv = NULL;
    json_t *c = NULL;
    int save_errno;

    if (context_fmt) {
        if (!(c = json_vpack_ex (NULL, 0, context_fmt, ap))) {
            errno = EINVAL;
            goto error;
        }
    }
    rv = entry_build (timestamp, name, c);
 error:
    save_errno = errno;
    json_decref (c);
    errno = save_errno;
    return rv;
}

json_t *eventlog_entry_pack (double timestamp,
                             const char *name,
                             const char *context_fmt,
                             ...)
{
    va_list ap;
    json_t *rv;

    va_start (ap, context_fmt);
    rv = eventlog_entry_vpack (timestamp,
                               name,
                               context_fmt,
                               ap);
    va_end (ap);
    return rv;
}

char *eventlog_entry_encode (json_t *entry)
{
    char *s = NULL;
    char *rv = NULL;

    if (!entry || !eventlog_entry_validate (entry)) {
        errno = EINVAL;
        return NULL;
    }
    if (!(s = json_dumps (entry, JSON_COMPACT))) {
        errno = ENOMEM;
        return NULL;
    }
    if (asprintf (&rv, "%s\n", s) < 0) {
        free (s);
        return NULL;
    }

    free (s);
    return rv;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
