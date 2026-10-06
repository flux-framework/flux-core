/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* proplist.c - build a systemd unit property array (RFC 52 encoding)
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/errprintf.h"

#include "proplist.h"

struct sdexec_proplist {
    json_t *props;
    int errnum;
    flux_error_t error;
};

struct sdexec_proplist *sdexec_proplist_create (void)
{
    struct sdexec_proplist *pl;

    if (!(pl = calloc (1, sizeof (*pl))))
        return NULL;
    if (!(pl->props = json_array ())) {
        free (pl);
        errno = ENOMEM;
        return NULL;
    }
    return pl;
}

void sdexec_proplist_destroy (struct sdexec_proplist *pl)
{
    if (pl) {
        int saved_errno = errno;
        json_decref (pl->props);
        free (pl);
        errno = saved_errno;
    }
}

static void set_error (struct sdexec_proplist *pl,
                       int errnum,
                       const char *name,
                       const char *msg)
{
    if (pl->errnum == 0) {
        pl->errnum = errnum;
        errprintf (&pl->error, "%s: %s", name ? name : "(null)", msg);
    }
}

/* RFC 52: x and t are decimal strings; other integers are JSON integers.
 */
static json_t *encode_int64 (int64_t i)
{
    char s[32];
    snprintf (s, sizeof (s), "%" PRId64, i);
    return json_string (s);
}

static json_t *encode_uint64 (uint64_t u)
{
    char s[32];
    snprintf (s, sizeof (s), "%" PRIu64, u);
    return json_string (s);
}

/* Encode one value of basic type 'type' from 'ap'.
 */
static json_t *encode_basic (char type, va_list *ap)
{
    switch (type) {
        case 'b':
            return json_boolean (va_arg (*ap, int));
        case 'y':
        case 'n':
        case 'q':
        case 'i':
        case 'h':
            return json_integer (va_arg (*ap, int));
        case 'u':
            return json_integer (va_arg (*ap, uint32_t));
        case 'x':
            return encode_int64 (va_arg (*ap, int64_t));
        case 't':
            return encode_uint64 (va_arg (*ap, uint64_t));
        case 'd':
            return json_real (va_arg (*ap, double));
        case 's':
        case 'o':
        case 'g': {
            const char *s = va_arg (*ap, const char *);
            return s ? json_string (s) : NULL;
        }
        default:
            return NULL;
    }
}

/* Encode element 'i' of fixed-size type 'type' at 'ptr'.
 */
static json_t *encode_element (char type, const void *ptr, size_t i)
{
    switch (type) {
        case 'y':
            return json_integer (((const uint8_t *)ptr)[i]);
        case 'b':
            return json_boolean (((const int *)ptr)[i]);
        case 'n':
            return json_integer (((const int16_t *)ptr)[i]);
        case 'q':
            return json_integer (((const uint16_t *)ptr)[i]);
        case 'i':
            return json_integer (((const int32_t *)ptr)[i]);
        case 'u':
            return json_integer (((const uint32_t *)ptr)[i]);
        case 'x':
            return encode_int64 (((const int64_t *)ptr)[i]);
        case 't':
            return encode_uint64 (((const uint64_t *)ptr)[i]);
        case 'd':
            return json_real (((const double *)ptr)[i]);
        default:
            return NULL;
    }
}

/* Append [name, [type, val]] to the property array.  Steals 'val'.
 */
static void append (struct sdexec_proplist *pl,
                    const char *name,
                    const char *type,
                    json_t *val)
{
    json_t *o;

    if (!val) {
        set_error (pl, EINVAL, name, "invalid value");
        return;
    }
    if (!(o = json_pack ("[s[so]]", name, type, val))) {
        set_error (pl, ENOMEM, name, "out of memory");
        return;
    }
    if (json_array_append_new (pl->props, o) < 0)
        set_error (pl, ENOMEM, name, "out of memory");
}

static bool valid_args (struct sdexec_proplist *pl,
                        const char *name,
                        const char *type)
{
    if (!pl)
        return false;
    if (pl->errnum != 0) // sticky error
        return false;
    if (!name || !type) {
        set_error (pl, EINVAL, name, "invalid argument");
        return false;
    }
    return true;
}

void sdexec_proplist_add (struct sdexec_proplist *pl,
                          const char *name,
                          const char *type,
                          ...)
{
    va_list ap;
    json_t *val;

    if (!valid_args (pl, name, type))
        return;
    if (strlen (type) != 1) {
        set_error (pl, EINVAL, name, "type is not a basic type");
        return;
    }
    va_start (ap, type);
    val = encode_basic (type[0], &ap);
    va_end (ap);
    append (pl, name, type, val);
}

void sdexec_proplist_add_array (struct sdexec_proplist *pl,
                                const char *name,
                                const char *type,
                                const void *ptr,
                                size_t count)
{
    char atype[3];
    json_t *a;

    if (!valid_args (pl, name, type))
        return;
    if (strlen (type) != 1 || (count > 0 && !ptr)) {
        set_error (pl, EINVAL, name, "invalid array");
        return;
    }
    if (!(a = json_array ())) {
        set_error (pl, ENOMEM, name, "out of memory");
        return;
    }
    for (size_t i = 0; i < count; i++) {
        json_t *o;
        if (!(o = encode_element (type[0], ptr, i))
            || json_array_append_new (a, o) < 0) {
            set_error (pl, EINVAL, name, "invalid array element");
            json_decref (a);
            return;
        }
    }
    snprintf (atype, sizeof (atype), "a%c", type[0]);
    append (pl, name, atype, a);
}

void sdexec_proplist_add_json (struct sdexec_proplist *pl,
                               const char *name,
                               const char *type,
                               json_t *val)
{
    if (!valid_args (pl, name, type))
        return;
    append (pl, name, type, json_incref (val));
}

json_t *sdexec_proplist_finish (struct sdexec_proplist *pl,
                                flux_error_t *error)
{
    if (!pl) {
        errprintf (error, "invalid argument");
        errno = EINVAL;
        return NULL;
    }
    if (pl->errnum != 0) {
        if (error)
            *error = pl->error;
        errno = pl->errnum;
        return NULL;
    }
    return json_incref (pl->props);
}

// vi:ts=4 sw=4 expandtab
