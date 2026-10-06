/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* proplist.h - build a systemd unit property array
 *
 * StartTransientUnit takes unit properties as D-Bus type a(sv), which
 * RFC 52 encodes as a JSON array of [name, [signature, value]] pairs.
 * The proplist builder hides that encoding: callers add properties as
 * C values, typed the same way as sd_bus_message_append(3).
 *
 * Errors are sticky.  After the first failed add, later adds do nothing,
 * and sdexec_proplist_finish() reports the first error.
 */

#ifndef _LIBSDEXEC_PROPLIST_H
#define _LIBSDEXEC_PROPLIST_H

#include <stddef.h>
#include <jansson.h>
#include <flux/core.h>

struct sdexec_proplist;

struct sdexec_proplist *sdexec_proplist_create (void);
void sdexec_proplist_destroy (struct sdexec_proplist *pl);

/* Add property 'name' with a value of basic D-Bus type 'type', passed as
 * for sd_bus_message_append(3):
 *   y, b, n, q, i, h  int
 *   u                 uint32_t
 *   x                 int64_t
 *   t                 uint64_t
 *   d                 double
 *   s, o, g           const char *
 */
void sdexec_proplist_add (struct sdexec_proplist *pl,
                          const char *name,
                          const char *type,
                          ...);

/* Add property 'name' with a value of type "a" + 'type', from 'count'
 * elements of basic fixed-size D-Bus type 'type' at 'ptr', as for
 * sd_bus_message_append_array(3), e.g. type "y" for a byte array.
 */
void sdexec_proplist_add_array (struct sdexec_proplist *pl,
                                const char *name,
                                const char *type,
                                const void *ptr,
                                size_t count);

/* Add property 'name' with value 'val', already encoded as RFC 52 JSON
 * for D-Bus type 'type', e.g. a JSON array of strings for "as".
 * The caller retains its reference to 'val'.
 */
void sdexec_proplist_add_json (struct sdexec_proplist *pl,
                               const char *name,
                               const char *type,
                               json_t *val);

/* Return the property array (a new reference), or NULL with errno set
 * and 'error' describing the first failed add.
 */
json_t *sdexec_proplist_finish (struct sdexec_proplist *pl,
                                flux_error_t *error);

#endif /* !_LIBSDEXEC_PROPLIST_H */

// vi:ts=4 sw=4 expandtab
