/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_INSTANCE_NAME_H
#define _UTIL_INSTANCE_NAME_H

#include <stdbool.h>
#include <stddef.h>

/* Longest instance name, excluding the terminating NUL.  This matches the
 * PMI KVS name maximum, which is the tightest constraint a name must live
 * within, and is far longer than a name should be in practice: a subinstance
 * appends its job ID to it, and the total must still fit.
 */
#define INSTANCE_NAME_MAX 63

/* Render 's' so it may be used where a program element name is expected,
 * such as a systemd unit name or a PMI KVS name.  Path separators become
 * dashes, the "ƒ" of the F58 job ID encoding becomes the plain "f" of the
 * f58plain encoding, and any other unsuitable character is dropped.  A
 * leading separator is suppressed.  The result is truncated to fit 'size'.
 * Ex: "/ƒ81DcDV/ƒ5HLm9" -> "f81DcDV-f5HLm9"
 *
 * The function is idempotent: sanitizing an already sanitized string
 * returns it unchanged.
 */
void instance_name_sanitize (const char *s, char *buf, size_t size);

/* Test whether 's' may be used as an instance name as is.  A name is valid
 * if it is non-empty, fits INSTANCE_NAME_MAX, and sanitizing leaves it
 * unchanged.  Defining it in terms of instance_name_sanitize() keeps the
 * two from drifting apart.
 *
 * Use this to check a name supplied by a user rather than sanitizing it, so
 * that a caller who sets a name can rely on finding that same name later.
 */
bool instance_name_valid (const char *s);

#endif /* !_UTIL_INSTANCE_NAME_H */

// vi:ts=4 sw=4 expandtab
