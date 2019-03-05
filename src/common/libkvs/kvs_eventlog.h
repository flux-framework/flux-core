/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_KVS_EVENTLOG_H
#define _FLUX_CORE_KVS_EVENTLOG_H

#ifdef __cplusplus
extern "C" {
#endif

/* RFC 18 KVS Event Log
 *
 * Events are of the form:
 *   "timestamp name [context ...]\n"
 */

#define FLUX_KVS_MAX_EVENT_NAME      (64)
#define FLUX_KVS_MAX_EVENT_CONTEXT   (256)

enum kvs_eventlog_op {
    FLUX_KVS_EVENTLOG_WATCH = 1
};

/* Create/destroy an eventlog
 */
struct flux_kvs_eventlog *flux_kvs_eventlog_create (void);
void flux_kvs_eventlog_destroy (struct flux_kvs_eventlog *eventlog);

/* Encode/decode an eventlog to/from a string (caller must free)
 */
char *flux_kvs_eventlog_encode (const struct flux_kvs_eventlog *eventlog);
struct flux_kvs_eventlog *flux_kvs_eventlog_decode (const char *s);

/* Append encoded event(s) to eventlog.
 */
int flux_kvs_eventlog_append (struct flux_kvs_eventlog *eventlog,
                              const char *s);

/* Iterator for events.
 */
const char *flux_kvs_eventlog_first (struct flux_kvs_eventlog *eventlog);
const char *flux_kvs_eventlog_next (struct flux_kvs_eventlog *eventlog);

/* Encode/decode an event.
 * flux_kvs_event_encode() sets timestamp to current wallclock.
 * flux_kvs_event_encode_timestamp() allows timestamp to be set to any value.
 * Caller must free return value of flux_kvs_event_encode().
 */
int flux_kvs_event_decode (const char *s,
                           double *timestamp,
                           char *name, int name_size,
                           char *context, int context_size);
char *flux_kvs_event_encode (const char *name, const char *context);
char *flux_kvs_event_encode_timestamp (double timestamp,
                                       const char *name,
                                       const char *context);

/* Eventlog streaming functions, return a single event per response.
 * The flux_kvs_eventlog_lookup_cancel() function can be called
 * to end the stream early.
 */
flux_future_t *flux_kvs_eventlog_lookup (flux_t *h, int flags, const char *key);
int flux_kvs_eventlog_lookup_get (flux_future_t *f, const char **event);
int flux_kvs_eventlog_lookup_cancel (flux_future_t *f);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_EVENTLOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
