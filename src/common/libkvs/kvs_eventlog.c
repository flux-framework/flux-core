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

static const char *strnchr (const char *s, int c, size_t size)
{
    int i;
    for (i = 0; i < size; i++)
        if (s[i] == c)
            return &s[i];
    return NULL;
}

int flux_kvs_event_decode (const char *s,
                           double *timestamp,
                           char *name, int name_size,
                           char *context, int context_size)
{
    const char *input;
    double t;
    char *cp;
    size_t toklen;

    if (!s)
        goto error_inval;
    input = s;

    /* time */
    if (!isdigit (*input))
        goto error_inval;
    errno = 0;
    t = strtod (input, &cp);
    if (errno != 0 || *cp != ' ' || t <= 0)
        goto error_inval;
    if (timestamp)
        *timestamp = t;
    input = cp + 1;

    /* name */
    if (!(cp = strchr (input, ' ')) && !(cp = strchr (input, '\n')))
        goto error_inval;
    if ((toklen = cp - input) > FLUX_KVS_MAX_EVENT_NAME || toklen == 0)
        goto error_inval;
    if (strnchr (input, '\n', toklen))
        goto error_inval;
    if (name) {
        if (name_size < toklen + 1)
            goto error_inval;
        memcpy (name, input, toklen);
        name[toklen] = '\0';
    }
    input = cp + 1;

    /* context (optional) */
    if (*cp == '\n') {
        if (context)
            context[0] = '\0';
    }
    else {
        if (!(cp = strchr (input, '\n')))
            goto error_inval;
        if ((toklen = cp - input) > FLUX_KVS_MAX_EVENT_CONTEXT)
            goto error_inval;
        if (context) {
            if (context_size < toklen + 1)
                goto error_inval;
            memcpy (context, input, toklen);
            context[toklen] = '\0';
        }
        input = cp + 1;
    }

    if (*input != '\0')
        goto error_inval;
    return 0;
error_inval:
    errno = EINVAL;
    return -1;
}

char *flux_kvs_event_encode_timestamp (double timestamp, const char *name,
                                       const char *context)
{
    char *s;
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
    if (asprintf (&s, "%.6f %s%s%s\n",
                  timestamp,
                  name,
                  context ? " " : "",
                  context ? context : "") < 0)
        return NULL;
    return s;
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

static int validate_lookup_flags (int flags)
{
    if (flags & ~FLUX_KVS_EVENTLOG_WATCH)
        return -1;
    return 0;
}

flux_future_t *flux_kvs_eventlog_lookup (flux_t *h, int flags, const char *key)
{
    flux_future_t *f;
    const char *topic = "eventlog-proxy.lookup";
    int rpc_flags = FLUX_RPC_STREAMING;

    if (!h || validate_lookup_flags (flags) < 0 || !key) {
        errno = EINVAL;
        return NULL;
    }
    if (!(f = flux_rpc_pack (h, topic, FLUX_NODEID_ANY, rpc_flags,
                             "{s:s s:i}",
                             "key", key,
                             "flags", flags)))
        return NULL;
    return f;
}

int flux_kvs_eventlog_lookup_get (flux_future_t *f, const char **event)
{
    const char *s;

    if (flux_rpc_get_unpack (f, "{s:s}", "event", &s) < 0)
        return -1;
    if (event)
        *event = s;
    return 0;
}

int flux_kvs_eventlog_lookup_cancel (flux_future_t *f)
{
    flux_future_t *f2;

    if (!f) {
        errno = EINVAL;
        return -1;
    }
    if (!(f2 = flux_rpc_pack (flux_future_get_flux (f),
                              "eventlog-proxy.cancel",
                              FLUX_NODEID_ANY,
                              FLUX_RPC_NORESPONSE,
                              "{s:i}",
                              "matchtag", (int)flux_rpc_get_matchtag (f))))
        return -1;
    flux_future_destroy (f2);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
