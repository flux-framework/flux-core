/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
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

#include "kvs_eventlog.h"


/* eventlog is a list of RFC 18 events.
 * Once appended to the list, pointers to the events can be accessed by users
 * with the guarantee that they remain valid until the eventlog is destroyed.
 */
struct flux_kvs_eventlog {
    zlist_t *events; // zlist cursor reserved for internal use
    int cursor;
};


void flux_kvs_eventlog_destroy (struct flux_kvs_eventlog *eventlog)
{
    if (eventlog) {
        int saved_errno = errno;
        if (eventlog->events) {
            char *s;
            while ((s = zlist_pop (eventlog->events)))
                free (s);
            zlist_destroy (&eventlog->events);
        }
        free (eventlog);
        errno = saved_errno;
    }
}

struct flux_kvs_eventlog *flux_kvs_eventlog_create (void)
{
    struct flux_kvs_eventlog *eventlog;

    if (!(eventlog = calloc (1, sizeof (*eventlog))))
        return NULL;
    if (!(eventlog->events = zlist_new ())) {
        errno = ENOMEM;
        goto error;
    }
    return eventlog;
error:
    flux_kvs_eventlog_destroy (eventlog);
    return NULL;
}

/* Determine the buffer size (including terminating \0) needed
 * to encode 'eventlog'.
 */
static size_t eventlog_encode_size (const struct flux_kvs_eventlog *eventlog)
{
    char *event;
    size_t size = 0;

    event = zlist_first (eventlog->events);
    while ((event)) {
        size += strlen (event);
        event = zlist_next (eventlog->events);
    }
    return size + 1; // plus \0 terminator
}

char *flux_kvs_eventlog_encode (const struct flux_kvs_eventlog *eventlog)
{
    char *cpy;
    char *event;
    size_t size;
    size_t used = 0;

    if (!eventlog) {
        errno = EINVAL;
        return NULL;
    }
    size = eventlog_encode_size (eventlog);
    if (!(cpy = calloc (1, size)))
        return NULL;
    event = zlist_first (eventlog->events);
    while (event) {
        int n;
        n = snprintf (cpy + used, size - used, "%s", event);
        assert (n < size - used);
        used += n;
        event = zlist_next (eventlog->events);
    }
    return cpy;
}

/* 'pp' is an in/out parameter pointing to input buffer.
 * Set 'tok' to next \n-terminated token, and 'toklen' to its length.
 * Advance 'pp' past token.  Returns false when input is exhausted.
 */
static bool eventlog_parse_next (const char **pp, const char **tok,
                                 size_t *toklen)
{
    char *term;

    if (!(term = strchr (*pp, '\n')))
        return false;
    *tok = *pp;
    *toklen = term - *pp + 1;
    *pp = term + 1;
    return true;
}

/* Return true if 'tok' contains a valid RFC 18 event
 */
static bool event_validate (const char *tok, size_t toklen)
{
    char *s;
    int rc;

    if (!(s = strndup (tok, toklen)))
        return false;
    rc = flux_kvs_event_decode (s, NULL, NULL, 0, NULL, 0);
    free (s);
    if (rc < 0)
        return false;
    return true;
}

struct flux_kvs_eventlog *flux_kvs_eventlog_decode (const char *s)
{
    struct flux_kvs_eventlog *eventlog;

    if (!(eventlog = flux_kvs_eventlog_create ()))
        return NULL;
    if (flux_kvs_eventlog_append (eventlog, s) < 0) {
        flux_kvs_eventlog_destroy (eventlog);
        return NULL;
    }
    return eventlog;
}

/* Return the nth (zero-origin) element of 'l', or NULL if it doesn't exist.
 */
static void *get_zlist_nth (zlist_t *l, int n)
{
    void *item;
    int count = 0;
    if (!l)
        return NULL;
    item = zlist_first (l);
    while (item && count++ < n)
        item = zlist_next (l);
    return item;
}

const char *flux_kvs_eventlog_next (struct flux_kvs_eventlog *eventlog)
{
    char *event;

    if (!eventlog)
        return NULL;
    if ((event = get_zlist_nth (eventlog->events, eventlog->cursor)))
        eventlog->cursor++;
    return event;
}

const char *flux_kvs_eventlog_first (struct flux_kvs_eventlog *eventlog)
{
    if (!eventlog)
        return NULL;
    eventlog->cursor = 0;
    return flux_kvs_eventlog_next (eventlog);
}

int flux_kvs_eventlog_append (struct flux_kvs_eventlog *eventlog,
                              const char *s)
{
    const char *input;
    const char *tok;
    size_t toklen;

    if (!eventlog || !s)
        goto error_inval;
    input = s;
    while (eventlog_parse_next (&input, &tok, &toklen)) {
        char *cpy;
        if (!event_validate (tok, toklen))
            goto error_inval;
        if (!(cpy = strndup (tok, toklen)))
            goto error;
        if (zlist_append (eventlog->events, cpy) < 0) {
            free (cpy);
            errno = ENOMEM;
            goto error;
        }
    }
    if (*input != '\0')
        goto error_inval;
    return 0;
error_inval:
    errno = EINVAL;
error:
    return -1;
}

int flux_kvs_event_decode (const char *s,
                           double *timestamp,
                           char *name, int name_size,
                           char *context, int context_size)
{
    json_t *o = NULL;
    int len;
    double t;
    const char *n;
    json_t *c;
    char *cstr = NULL;
    int rv = -1;

    if (!s)
        goto error_inval;

    if (!(len = strlen (s)))
        goto error_inval;

    if (s[len - 1] != '\n')
        goto error_inval;

    if (!(o = json_loads (s, 0, NULL)))
        goto error_inval;

    if (json_unpack (o, "{ s:F s:s }",
                     "timestamp", &t,
                     "name", &n) < 0)
        goto error_inval;

    if (timestamp)
        *timestamp = t;
    if (name) {
        int name_len = strlen (n);
        if (name_len > FLUX_KVS_MAX_EVENT_NAME)
            goto error_inval;
        if (name_size <= name_len)
            goto error_inval;
        strcpy (name, n);
    }

    if (!json_unpack (o, "{ s:o }", "context", &c)) {
        int context_len;
        if (!(cstr = json_dumps (c, JSON_COMPACT)))
            goto error_inval;
        context_len = strlen (cstr);
        if (context_len > FLUX_KVS_MAX_EVENT_CONTEXT)
            goto error_inval;
        if (context) {
            if (context_size <= context_len)
                goto error_inval;
            strcpy (context, cstr);
        }
    }
    else {
        if (context)
            context[0] = '\0';
    }

    rv = 0;
    goto out;

error_inval:
    errno = EINVAL;
out:
    json_decref (o);
    free (cstr);
    return rv;
}

char *flux_kvs_event_encode_timestamp (double timestamp, const char *name,
                                       const char *context)
{
    json_t *o = NULL;
    char *s = NULL;
    char *rv = NULL;
    json_t *c = NULL;
    int namelen;

    if (!name || timestamp <= 0.
              || (namelen = strlen (name)) > FLUX_KVS_MAX_EVENT_NAME
              || namelen == 0
              || strchr (name, ' ') != NULL
              || strchr (name, '\n') != NULL
              || (context && (strchr (context, '\n') != NULL
                        || strlen (context) > FLUX_KVS_MAX_EVENT_CONTEXT))) {
        errno = EINVAL;
        return NULL;
    }
    if (!context || !strlen (context)) {
        if (!(o = json_pack ("{ s:f s:s }",
                             "timestamp", timestamp,
                             "name", name))) {
            errno = ENOMEM;
            goto error;
        }
    }
    else {
        /* verify context is a json object */
        if (!(c = json_loads (context, 0, NULL))) {
            errno = EINVAL;
            goto error;
        }
        if (!json_is_object (c)) {
            errno = EINVAL;
            goto error;
        }
        if (!(o = json_pack ("{ s:f s:s s:O }",
                             "timestamp", timestamp,
                             "name", name,
                             "context", c))) {
            errno = ENOMEM;
            goto error;
        }
    }
    if (!(s = json_dumps (o, JSON_COMPACT))) {
        errno = ENOMEM;
        goto error;
    }
    if (asprintf (&rv, "%s\n", s) < 0) {
        errno = ENOMEM;
        goto error;
    }
error:
    json_decref (c);
    json_decref (o);
    free (s);
    return rv;
}

static int get_timestamp_now (double *timestamp)
{
    struct timespec ts;
    if (clock_gettime (CLOCK_REALTIME, &ts) < 0)
        return -1;
    *timestamp = (1E-9 * ts.tv_nsec) + ts.tv_sec;
    return 0;
}


char *flux_kvs_event_encode (const char *name, const char *context)
{
    double timestamp;

    if (get_timestamp_now (&timestamp) < 0)
        return NULL;
    return flux_kvs_event_encode_timestamp (timestamp, name, context);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
