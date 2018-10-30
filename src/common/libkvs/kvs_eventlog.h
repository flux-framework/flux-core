/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

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


/* Create/destroy an eventlog
 */
struct flux_kvs_eventlog *flux_kvs_eventlog_create (void);
void flux_kvs_eventlog_destroy (struct flux_kvs_eventlog *eventlog);

/* Encode/decode an eventlog to/from a string (caller must free)
 */
char *flux_kvs_eventlog_encode (const struct flux_kvs_eventlog *eventlog);
struct flux_kvs_eventlog *flux_kvs_eventlog_decode (const char *s);

/* Update an eventlog with new encoded snapshot 's'.
 */
int flux_kvs_eventlog_update (struct flux_kvs_eventlog *eventlog,
                              const char *s);

/* Append an encoded event to eventlog.
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


#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_EVENTLOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
