/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* value.c - D-Bus values in their RFC 52 JSON encoding
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <jansson.h>
#include <flux/core.h>

#include "ccan/str/str.h"

#include "value.h"

/* Return the length of the single complete type at the start of 'sig',
 * or -1 if it is invalid.  D-Bus signatures are limited to 255 bytes,
 * which bounds the recursion.
 */
static int type_len (const char *sig);

/* Return the length of the dict entry type at the start of 'sig', or -1
 * if it is invalid.  Per the D-Bus specification, a dict entry has
 * exactly two members and its key is a basic type.  This is only called
 * for an array element type, the only place a dict entry may appear.
 */
static int dict_len (const char *sig)
{
    int n;

    if (sig[1] == '\0' || !strchr ("ybnqiuxtdsogh", sig[1]))
        return -1;
    if ((n = type_len (sig + 2)) < 0)
        return -1;
    if (sig[2 + n] != '}')
        return -1;
    return n + 3;
}

static int type_len (const char *sig)
{
    switch (sig[0]) {
        case 'a': {
            int n;
            if (sig[1] == '{')
                n = dict_len (sig + 1);
            else
                n = type_len (sig + 1);
            return n < 0 ? -1 : n + 1;
        }
        case '(': {
            int i = 1;
            while (sig[i] != '\0' && sig[i] != ')') {
                int n = type_len (sig + i);
                if (n < 0)
                    return -1;
                i += n;
            }
            return sig[i] == ')' && i > 1 ? i + 1 : -1;
        }
        case '\0':
            return -1;
        default:
            return strchr ("ybnqiuxtdsoghv", sig[0]) ? 1 : -1;
    }
}

static bool is_string_type (char c)
{
    return c == 's' || c == 'o' || c == 'g';
}

/* Encoding
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

json_t *sdexec_value_encode (char type, va_list *ap)
{
    switch (type) {
        case 'b':
            return json_boolean (va_arg (*ap, int));
        case 'y':
        case 'n':
        case 'q':
        case 'i':
            return json_integer (va_arg (*ap, int));
        case 'h':
            // RFC 52: h is an object with fd and pid members
            return json_pack ("{s:i s:i}",
                              "fd", va_arg (*ap, int),
                              "pid", (int)getpid ());
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

json_t *sdexec_value_encode_element (char type, const void *ptr, size_t i)
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

/* Reading
 */

static int read_value (json_t *val, const char *type, int len, va_list *ap);

static int read_integer (json_t *val, json_int_t min, json_int_t max,
                         json_int_t *ip)
{
    json_int_t i;

    if (!json_is_integer (val))
        return -1;
    i = json_integer_value (val);
    if (i < min || i > max)
        return -1;
    *ip = i;
    return 0;
}

/* RFC 52: x and t are decimal strings, with no leading zeros, no leading
 * '+', and no whitespace.
 */
static int read_decimal (json_t *val, bool is_signed, int64_t *xp,
                         uint64_t *tp)
{
    const char *s;
    const char *digits;
    char *endptr;

    if (!json_is_string (val))
        return -1;
    s = json_string_value (val);
    digits = (is_signed && s[0] == '-') ? s + 1 : s;
    if (digits[0] < '0' || digits[0] > '9'
        || (digits[0] == '0' && digits[1] != '\0')
        || (digits != s && streq (digits, "0")))
        return -1;
    errno = 0;
    if (is_signed)
        *xp = strtoimax (s, &endptr, 10);
    else
        *tp = strtoumax (s, &endptr, 10);
    if (errno != 0 || *endptr != '\0')
        return -1;
    return 0;
}

static int read_basic (json_t *val, char type, va_list *ap)
{
    json_int_t i;

    switch (type) {
        case 'b':
            if (!json_is_boolean (val))
                return -1;
            *va_arg (*ap, int *) = json_is_true (val) ? 1 : 0;
            return 0;
        case 'y':
            if (read_integer (val, 0, UINT8_MAX, &i) < 0)
                return -1;
            *va_arg (*ap, uint8_t *) = i;
            return 0;
        case 'n':
            if (read_integer (val, INT16_MIN, INT16_MAX, &i) < 0)
                return -1;
            *va_arg (*ap, int16_t *) = i;
            return 0;
        case 'q':
            if (read_integer (val, 0, UINT16_MAX, &i) < 0)
                return -1;
            *va_arg (*ap, uint16_t *) = i;
            return 0;
        case 'i':
            if (read_integer (val, INT32_MIN, INT32_MAX, &i) < 0)
                return -1;
            *va_arg (*ap, int32_t *) = i;
            return 0;
        case 'u':
            if (read_integer (val, 0, UINT32_MAX, &i) < 0)
                return -1;
            *va_arg (*ap, uint32_t *) = i;
            return 0;
        case 'h': {
            // RFC 52: the pid member must match the reading process
            json_int_t fd;
            json_int_t pid;
            if (json_unpack (val, "{s:I s:I}", "fd", &fd, "pid", &pid) < 0
                || fd < 0
                || fd > INT32_MAX
                || pid != getpid ())
                return -1;
            *va_arg (*ap, int *) = fd;
            return 0;
        }
        case 'x': {
            int64_t x;
            if (read_decimal (val, true, &x, NULL) < 0)
                return -1;
            *va_arg (*ap, int64_t *) = x;
            return 0;
        }
        case 't': {
            uint64_t t;
            if (read_decimal (val, false, NULL, &t) < 0)
                return -1;
            *va_arg (*ap, uint64_t *) = t;
            return 0;
        }
        case 'd':
            if (!json_is_number (val))
                return -1;
            *va_arg (*ap, double *) = json_number_value (val);
            return 0;
        case 's':
        case 'o':
        case 'g':
            if (!json_is_string (val))
                return -1;
            *va_arg (*ap, const char **) = json_string_value (val);
            return 0;
        default:
            return -1;
    }
}

/* Read each single complete type in sig[0:len] from successive elements
 * of JSON array 'a'.
 */
static int read_sequence (json_t *a, const char *sig, int len, va_list *ap)
{
    size_t index = 0;
    int i = 0;

    if (!json_is_array (a))
        return -1;
    while (i < len) {
        int n = type_len (sig + i);
        if (n < 0
            || read_value (json_array_get (a, index++), sig + i, n, ap) < 0)
            return -1;
        i += n;
    }
    return index == json_array_size (a) ? 0 : -1;
}

static int read_variant (json_t *val, const char *type, va_list *ap)
{
    const char *contents;
    json_t *v;
    int n;

    if (!type
        || (n = type_len (type)) < 0
        || n != strlen (type)
        || json_unpack (val, "[so!]", &contents, &v) < 0
        || !streq (contents, type))
        return -1;
    return read_value (v, type, n, ap);
}

static int read_value (json_t *val, const char *type, int len, va_list *ap)
{
    if (!val)
        return -1;
    if (type[0] == 'v')
        return read_variant (val, va_arg (*ap, const char *), ap);
    if (type[0] == '(')
        return read_sequence (val, type + 1, len - 2, ap);
    if (type[0] == 'a') {
        // RFC 52: a dict with a string key is an object, else an array
        bool is_object = type[1] == '{' && is_string_type (type[2]);
        if (is_object ? !json_is_object (val) : !json_is_array (val))
            return -1;
        *va_arg (*ap, json_t **) = val;
        return 0;
    }
    if (len == 1)
        return read_basic (val, type[0], ap);
    return -1;
}

int sdexec_value_read (json_t *val, const char *type, ...)
{
    va_list ap;
    int n;
    int rc;

    if (!val || !type || (n = type_len (type)) < 0 || n != strlen (type)) {
        errno = EINVAL;
        return -1;
    }
    va_start (ap, type);
    rc = read_value (val, type, n, &ap);
    va_end (ap);
    if (rc < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int sdexec_variant_vread (json_t *val, const char *type, va_list *ap)
{
    if (!val || !type) {
        errno = EINVAL;
        return -1;
    }
    if (read_variant (val, type, ap) < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int sdexec_variant_read (json_t *val, const char *type, ...)
{
    va_list ap;
    int rc;

    va_start (ap, type);
    rc = sdexec_variant_vread (val, type, &ap);
    va_end (ap);
    return rc;
}

static int params_vread (json_t *params, const char *sig, va_list *ap)
{
    if (!params || !sig || strlen (sig) > 255) {
        errno = EINVAL;
        return -1;
    }
    if (read_sequence (params, sig, strlen (sig), ap) < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int sdexec_params_read (json_t *params, const char *sig, ...)
{
    va_list ap;
    int rc;

    va_start (ap, sig);
    rc = params_vread (params, sig, &ap);
    va_end (ap);
    return rc;
}

int sdexec_reply_read (flux_future_t *f, const char *sig, ...)
{
    const char *rsig;
    json_t *params;
    va_list ap;
    int rc;

    if (!f || !sig) {
        errno = EINVAL;
        return -1;
    }
    if (flux_rpc_get_unpack (f,
                             "{s:s s:o}",
                             "signature", &rsig,
                             "params", &params) < 0)
        return -1;
    if (!streq (rsig, sig)) {
        errno = EPROTO;
        return -1;
    }
    va_start (ap, sig);
    rc = params_vread (params, sig, &ap);
    va_end (ap);
    return rc;
}

// vi:ts=4 sw=4 expandtab
