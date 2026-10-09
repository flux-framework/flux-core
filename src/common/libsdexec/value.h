/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* value.h - read D-Bus values in their RFC 52 JSON encoding
 *
 * Values are read into C variables named by D-Bus type, in the manner of
 * sd_bus_message_read(3):
 *   y  uint8_t *        b  int *            n  int16_t *
 *   q  uint16_t *       i  int32_t *        u  uint32_t *
 *   x  int64_t *        t  uint64_t *       d  double *
 *   h  int *            s, o, g  const char **
 *   (...)  one argument per member, in order
 *   v      the expected contents type (const char *), then its arguments
 *   a...   json_t ** (the RFC 52 JSON array or object)
 *
 * Strings and json_t pointers are borrowed from the JSON being read.
 * A value that does not match its type fails with EPROTO, including a
 * variant whose contents type differs from the one expected.
 */

#ifndef _LIBSDEXEC_VALUE_H
#define _LIBSDEXEC_VALUE_H

#include <stdarg.h>
#include <stddef.h>
#include <jansson.h>
#include <flux/core.h>

/* Read 'val', a value of single complete type 'type'.
 */
int sdexec_value_read (json_t *val, const char *type, ...);

/* Read 'val', a variant expected to contain a value of single complete
 * type 'type'.
 */
int sdexec_variant_read (json_t *val, const char *type, ...);
int sdexec_variant_vread (json_t *val, const char *type, va_list *ap);

/* Read 'params', a message body with signature 'sig'.
 */
int sdexec_params_read (json_t *params, const char *sig, ...);

/* Read the body of the current sdbus.call or sdbus.subscribe response
 * in 'f', failing with EPROTO if its signature is not 'sig'.
 */
int sdexec_reply_read (flux_future_t *f, const char *sig, ...);

/* Encode one value of basic type 'type' from 'ap', in the manner of
 * sd_bus_message_append(3).  Returns NULL if 'type' is not a basic type.
 */
json_t *sdexec_value_encode (char type, va_list *ap);

/* Encode element 'index' of an array of fixed-size basic type 'type'
 * at 'ptr', in the manner of sd_bus_message_append_array(3).
 */
json_t *sdexec_value_encode_element (char type, const void *ptr, size_t index);

#endif /* !_LIBSDEXEC_VALUE_H */

// vi:ts=4 sw=4 expandtab
