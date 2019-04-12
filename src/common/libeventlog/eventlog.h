/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _EVENTLOG_H
#define _EVENTLOG_H

#include <jansson.h>

/* convenience function to extract timestamp, name, and optional
 * context from an event entry */
int eventlog_entry_parse (json_t *entry,
                          double *timestamp,
                          const char **name,
                          json_t **context);

/* decode an eventlog into an json array of event objects */
json_t *eventlog_decode (const char *s);

/* decode a single eventlog entry into a json object */
json_t *eventlog_entry_decode (const char *entry);

/* build an eventlog entry, context must be a json object */
json_t *eventlog_entry (double timestamp, const char *name,
                        const char *context);

/* build an eventlog entry using jansson style pack, context must be
 * a json object */
json_t *eventlog_entry_pack (double timestamp,
                             const char *name,
                             const char *context_fmt,
                             ...);
json_t *eventlog_entry_vpack (double timestamp,
                              const char *name,
                              const char *context_fmt,
                              va_list ap);

char *eventlog_entry_encode (json_t *entry);

#endif /* !_EVENTLOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

