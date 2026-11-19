/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBSDEXEC_RECORD_H
#define _LIBSDEXEC_RECORD_H

#include <jansson.h>

/* A unit record is the durable copy of what sdexec knows about a transient
 * unit, kept outside systemd's unit table so it survives a module restart
 * and the garbage collection of the unit itself.  It is an RFC 18 eventlog
 * in 'dir', named after the unit, with two writers:
 *
 *   start  written by sdexec before the unit is started, with a context
 *          of whatever the module needs to re-adopt the unit (label,
 *          command line, flags, ...).
 *   exit   appended by systemd, via the unit's ExecStopPost command, when
 *          the unit's main process has exited, with a context of
 *          {"result":s, "code":s, "status":s} taken from SERVICE_RESULT,
 *          EXIT_CODE and EXIT_STATUS (see systemd.exec(5)).  This is the
 *          only trace of a successful exit once systemd has collected
 *          the unit.
 *
 * Events with other names are ignored by the readers below, so more may
 * be added later.  sdexec removes the record when it is done with the unit.
 */

/* Write a record for unit 'name' in 'dir' consisting of a "start" event
 * with context 'context' (atomically, via a temporary file and rename).
 * Return 0 on success, -1 with errno set.
 */
int sdexec_record_write (const char *dir, const char *name, json_t *context);

/* Read the "start" event context of the record for unit 'name' in 'dir'.
 * Return a new reference, or NULL with errno set (ENOENT if there is no
 * record, EPROTO if its first event is not a valid "start" event).
 */
json_t *sdexec_record_read (const char *dir, const char *name);

/* Read the exit status systemd recorded for unit 'name' in 'dir', as a
 * property dict containing ExecMainCode and ExecMainStatus suitable for
 * sdexec_unit_update().  Return a new reference, or NULL with errno set:
 * ENOENT if the unit has no record or has not recorded an exit; EPROTO if
 * the record cannot be parsed, including an exit event left incomplete
 * because its writer was killed.
 */
json_t *sdexec_record_read_exit (const char *dir, const char *name);

/* Remove the record for unit 'name' in 'dir'.  A missing record is not an
 * error.  Return 0 on success, -1 with errno set.
 */
int sdexec_record_remove (const char *dir, const char *name);

/* List the units that have records in 'dir' whose names end in 'suffix'
 * (use "" for all).  Return a new reference to a JSON array of name
 * strings, or NULL with errno set.
 */
json_t *sdexec_record_list (const char *dir, const char *suffix);

/* Remove temporary files left in 'dir' by an sdexec_record_write() that
 * was interrupted before its rename, for units whose names end in
 * 'suffix'.  Only call this when no record write for such a unit can be
 * in progress.  Return the number removed, or -1 with errno set.
 */
int sdexec_record_remove_tmp (const char *dir, const char *suffix);

/* Build the command line systemd should run as the unit's ExecStopPost
 * to record the exit status of unit 'name' in 'dir'.  Return a new
 * reference to a JSON array of strings, or NULL with errno set.
 */
json_t *sdexec_record_execstoppost (const char *dir, const char *name);

#endif /* !_LIBSDEXEC_RECORD_H */

// vi:ts=4 sw=4 expandtab
