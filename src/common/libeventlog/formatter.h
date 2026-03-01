/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _EVENTLOG_FORMATTER_H
#define _EVENTLOG_FORMATTER_H

#include <stdio.h>
#include <jansson.h>

#include <flux/core.h>

struct eventlog_formatter *eventlog_formatter_create (void);
void eventlog_formatter_destroy (struct eventlog_formatter *evf);

/*  Enable color if color=1, disable if color=0.
 *  Returns -1 with EINVAL if color is not 0 or 1.
 */
int eventlog_formatter_set_color (struct eventlog_formatter *evf, int color);

/*  Reset an eventlog formatter. (Clear t0 timestamp).
 *  Formatting options remain unchanged.
 */
void eventlog_formatter_reset (struct eventlog_formatter *evf);

/*  Update formatter initial timestamp if not currently set.
 */
void eventlog_formatter_update_t0 (struct eventlog_formatter *evf, double ts);

/*  Set the timestamp format for output. String 'format' may be one of
 *  "raw"    - raw double precision timestamp (default)
 *  "iso"    - ISO 8601
 *  "offset" - offset from initial timestamp in eventlog.
 */
int eventlog_formatter_set_timestamp_format (struct eventlog_formatter *evf,
                                             const char *format);

/*  Set eventlog entry format by name for formatter 'evf':
 *  "text" - formatted text (default)
 *  "json" - unformatted raw JSON output (if set, timestamp format is ignored)
 */
int eventlog_formatter_set_format (struct eventlog_formatter *evf,
                                   const char *format);

/*  Disable newline character after eventlog_entry_dumps/f().
 */
void eventlog_formatter_set_no_newline (struct eventlog_formatter *evf);

/*  Dump eventlog entry encoded by formatter 'evf' to a string.
 *  Caller must free result.
 *  Returns 0 on success, -1 with errno set and error text in errp->text.
 */
char *eventlog_entry_dumps (struct eventlog_formatter *evf,
                            flux_error_t *errp,
                            json_t *event);

/*  Dump the eventlog entry encoded by 'evf' to stream 'fp'.
 *  Returns 0 on success, -1 with errno set and error text in errp->text.
 */
int eventlog_entry_dumpf (struct eventlog_formatter *evf,
                          FILE *fp,
                          flux_error_t *errp,
                          json_t *event);


#endif /* !_EVENTLOG_FORMATTER_H */
