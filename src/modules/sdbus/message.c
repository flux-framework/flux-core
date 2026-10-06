/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* message.c - D-Bus message payload/JSON conversion helpers
 *
 * Values are encoded as described in RFC 52.  JSON to D-Bus conversion
 * is driven by the D-Bus signature.  D-Bus to JSON conversion needs no
 * signature since D-Bus messages are self-describing.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>

#include "ccan/str/str.h"
#include "ccan/array_size/array_size.h"

#include "objpath.h"
#include "message.h"

/* D-Bus specification: signatures are limited to 255 bytes.
 * This also bounds recursion in sig_type_len().
 */
#define SIG_MAX 255

typedef union {
    uint8_t y;
    int b;
    int16_t n;
    uint16_t q;
    int32_t i;
    uint32_t u;
    int64_t x;
    uint64_t t;
    int h;
    double d;
    const char *s;
} value_t;

struct tab {
    uint8_t type;
    const char *desc;
};

static struct tab typetab[] = {
    { SD_BUS_MESSAGE_METHOD_CALL, "method-call" },
    { SD_BUS_MESSAGE_METHOD_RETURN, "method-return" },
    { SD_BUS_MESSAGE_METHOD_ERROR, "method-error" },
    { SD_BUS_MESSAGE_SIGNAL , "signal" },
};

const char *sdmsg_typestr (sd_bus_message *m)
{
    uint8_t type;

    if (sd_bus_message_get_type (m, &type) >= 0) {
        for (int i = 0; i < ARRAY_SIZE (typetab); i++)
            if (typetab[i].type == type)
                return typetab[i].desc;
    }
    return "unknown";
}

static bool is_basic (char c)
{
    return c != '\0' && strchr ("ybnqiuxtdsogh", c) != NULL;
}

static bool is_string_type (char c)
{
    return c == 's' || c == 'o' || c == 'g';
}

/* Return the length of the single complete type at the start of 'sig',
 * or -EPROTO if it is invalid.
 */
static int sig_type_len (const char *sig);

/* Return the length of the dict entry type at the start of 'sig', or
 * -EPROTO if it is invalid.  Per the D-Bus specification, a dict entry
 * has exactly two members and its key is a basic type.  This is only
 * called for an array element type, the only place a dict entry may
 * appear.
 */
static int sig_dict_len (const char *sig)
{
    int n;

    if (!is_basic (sig[1]))
        return -EPROTO;
    if ((n = sig_type_len (sig + 2)) < 0)
        return n;
    if (sig[2 + n] != '}')
        return -EPROTO;
    return n + 3;
}

static int sig_type_len (const char *sig)
{
    switch (sig[0]) {
        case 'a': {
            int n;
            if (sig[1] == '{')
                n = sig_dict_len (sig + 1);
            else
                n = sig_type_len (sig + 1);
            return n < 0 ? n : n + 1;
        }
        case '(': {
            int i = 1;
            while (sig[i] != '\0' && sig[i] != ')') {
                int n = sig_type_len (sig + i);
                if (n < 0)
                    return n;
                i += n;
            }
            if (sig[i] != ')' || i == 1)
                return -EPROTO;
            return i + 1;
        }
        default:
            if (is_basic (sig[0]) || sig[0] == 'v')
                return 1;
            return -EPROTO;
    }
}

/* Return true if 's' is a valid D-Bus signature: zero or more single
 * complete types, within the length limit.
 */
static bool is_valid_signature (const char *s)
{
    size_t len = strlen (s);
    size_t i = 0;

    if (len > SIG_MAX)
        return false;
    while (i < len) {
        int n = sig_type_len (s + i);
        if (n < 0)
            return false;
        i += n;
    }
    return true;
}

/* JSON -> D-Bus
 */

static int put_value (sd_bus_message *m,
                      const char *type,
                      size_t len,
                      json_t *o);

/* RFC 52: x and t are encoded as JSON strings containing the value in
 * decimal, with no leading zeros, no leading +, and no whitespace.
 */
static int put_int64 (char type, json_t *o, value_t *v)
{
    const char *s;
    const char *digits;
    char *endptr;

    if (!json_is_string (o))
        return -EPROTO;
    s = json_string_value (o);
    digits = (type == 'x' && s[0] == '-') ? s + 1 : s;
    if (digits[0] < '0' || digits[0] > '9'
        || (digits[0] == '0' && digits[1] != '\0')
        || (digits != s && streq (digits, "0"))) // -0
        return -EPROTO;
    errno = 0;
    if (type == 'x')
        v->x = strtoimax (s, &endptr, 10);
    else
        v->t = strtoumax (s, &endptr, 10);
    if (errno != 0 || *endptr != '\0')
        return -EPROTO;
    return 0;
}

static int put_integer (char type, json_t *o, value_t *v)
{
    json_int_t i;

    if (!json_is_integer (o))
        return -EPROTO;
    i = json_integer_value (o);
    switch (type) {
        case 'y':
            if (i < 0 || i > UINT8_MAX)
                return -EPROTO;
            v->y = i;
            break;
        case 'n':
            if (i < INT16_MIN || i > INT16_MAX)
                return -EPROTO;
            v->n = i;
            break;
        case 'q':
            if (i < 0 || i > UINT16_MAX)
                return -EPROTO;
            v->q = i;
            break;
        case 'i':
            if (i < INT32_MIN || i > INT32_MAX)
                return -EPROTO;
            v->i = i;
            break;
        case 'u':
            if (i < 0 || i > UINT32_MAX)
                return -EPROTO;
            v->u = i;
            break;
        case 'h':
            if (i < INT32_MIN || i > INT32_MAX)
                return -EPROTO;
            v->h = i;
            break;
        default:
            return -EPROTO;
    }
    return 0;
}

static int put_string (sd_bus_message *m, char type, json_t *o)
{
    const char *s;
    int e;

    if (!json_is_string (o))
        return -EPROTO;
    s = json_string_value (o);
    if (strlen (s) != json_string_length (o)) // embedded NUL
        return -EPROTO;
    /* RFC 52: reject strings that are not valid for the D-Bus type.
     * sd-bus validation of g varies by systemd version, so do not
     * rely on it.
     */
    if (type == 'g' && !is_valid_signature (s))
        return -EPROTO;
    if (type == 'o') {
        char *path;
        if (!(path = objpath_encode (s)))
            return -errno;
        e = sd_bus_message_append_basic (m, type, path);
        free (path);
        return e;
    }
    return sd_bus_message_append_basic (m, type, s);
}

static int put_basic (sd_bus_message *m, char type, json_t *o)
{
    value_t v;
    int e;

    if (is_string_type (type))
        return put_string (m, type, o);
    switch (type) {
        case 'b':
            if (!json_is_boolean (o))
                return -EPROTO;
            v.b = json_is_true (o) ? 1 : 0;
            break;
        case 'd':
            if (!json_is_number (o))
                return -EPROTO;
            v.d = json_number_value (o);
            break;
        case 'x':
        case 't':
            if ((e = put_int64 (type, o, &v)) < 0)
                return e;
            break;
        default:
            if ((e = put_integer (type, o, &v)) < 0)
                return e;
            break;
    }
    return sd_bus_message_append_basic (m, type, &v);
}

/* Append one value for each single complete type in sig[0:len] from
 * successive elements of JSON array 'a'.
 */
static int put_sequence (sd_bus_message *m,
                         const char *sig,
                         size_t len,
                         json_t *a)
{
    size_t index = 0;
    size_t i = 0;
    int e;

    if (!json_is_array (a))
        return -EPROTO;
    while (i < len) {
        int n;
        json_t *entry;

        if ((n = sig_type_len (sig + i)) < 0)
            return n;
        if (!(entry = json_array_get (a, index++)))
            return -EPROTO;
        if ((e = put_value (m, sig + i, n, entry)) < 0)
            return e;
        i += n;
    }
    if (index != json_array_size (a))
        return -EPROTO;
    return 0;
}

static int put_variant (sd_bus_message *m, json_t *o)
{
    const char *vsig;
    json_t *val;
    int e;

    if (json_unpack (o, "[so!]", &vsig, &val) < 0
        || strlen (vsig) > SIG_MAX
        || sig_type_len (vsig) != strlen (vsig))
        return -EPROTO;
    if ((e = sd_bus_message_open_container (m, 'v', vsig)) < 0
        || (e = put_value (m, vsig, strlen (vsig), val)) < 0
        || (e = sd_bus_message_close_container (m)) < 0)
        return e;
    return 0;
}

static int put_struct (sd_bus_message *m,
                       const char *type,
                       size_t len,
                       json_t *o)
{
    char *contents;
    int e;

    if (!(contents = strndup (type + 1, len - 2)))
        return -ENOMEM;
    if ((e = sd_bus_message_open_container (m, 'r', contents)) >= 0
        && (e = put_sequence (m, type + 1, len - 2, o)) >= 0)
        e = sd_bus_message_close_container (m);
    free (contents);
    return e < 0 ? e : 0;
}

/* Append one dict entry from key 'k' and value 'v'.
 * 'type' points to the dict entry type, e.g. "{sv}".
 */
static int put_dict_entry (sd_bus_message *m,
                           const char *type,
                           size_t len,
                           const char *contents,
                           json_t *k,
                           json_t *v)
{
    int e;

    if (!k || !v)
        return -EPROTO;
    if ((e = sd_bus_message_open_container (m, 'e', contents)) < 0
        || (e = put_basic (m, type[1], k)) < 0
        || (e = put_value (m, type + 2, len - 3, v)) < 0
        || (e = sd_bus_message_close_container (m)) < 0)
        return e;
    return 0;
}

/* RFC 52: a dict with an s, o, or g key is a JSON object.  A dict with
 * any other key type is a JSON array of [key, value] pairs.
 * 'type' points to the dict entry type, e.g. "{sv}".
 */
static int put_dict (sd_bus_message *m,
                     const char *type,
                     size_t len,
                     json_t *o)
{
    char *contents;
    int e = 0;

    if (!is_basic (type[1]))
        return -EPROTO;
    if (!(contents = strndup (type + 1, len - 2)))
        return -ENOMEM;
    if (is_string_type (type[1])) {
        const char *key;
        json_t *val;

        if (!json_is_object (o)) {
            e = -EPROTO;
            goto done;
        }
        json_object_foreach (o, key, val) {
            json_t *k;

            if (!(k = json_string (key))) {
                e = -EPROTO;
                goto done;
            }
            e = put_dict_entry (m, type, len, contents, k, val);
            json_decref (k);
            if (e < 0)
                goto done;
        }
    }
    else {
        size_t index;
        json_t *pair;

        if (!json_is_array (o)) {
            e = -EPROTO;
            goto done;
        }
        json_array_foreach (o, index, pair) {
            if (!json_is_array (pair) || json_array_size (pair) != 2) {
                e = -EPROTO;
                goto done;
            }
            e = put_dict_entry (m,
                                type,
                                len,
                                contents,
                                json_array_get (pair, 0),
                                json_array_get (pair, 1));
            if (e < 0)
                goto done;
        }
    }
done:
    free (contents);
    return e;
}

static int put_array (sd_bus_message *m,
                      const char *type,
                      size_t len,
                      json_t *o)
{
    char *contents;
    int e;

    if (!(contents = strndup (type + 1, len - 1)))
        return -ENOMEM;
    if ((e = sd_bus_message_open_container (m, 'a', contents)) < 0)
        goto done;
    if (type[1] == '{') {
        if ((e = put_dict (m, type + 1, len - 1, o)) < 0)
            goto done;
    }
    else {
        size_t index;
        json_t *entry;

        if (!json_is_array (o)) {
            e = -EPROTO;
            goto done;
        }
        json_array_foreach (o, index, entry) {
            if ((e = put_value (m, type + 1, len - 1, entry)) < 0)
                goto done;
        }
    }
    e = sd_bus_message_close_container (m);
done:
    free (contents);
    return e < 0 ? e : 0;
}

static int put_value (sd_bus_message *m,
                      const char *type,
                      size_t len,
                      json_t *o)
{
    if (!o)
        return -EPROTO;
    if (len == 1 && is_basic (type[0]))
        return put_basic (m, type[0], o);
    if (len == 1 && type[0] == 'v')
        return put_variant (m, o);
    if (type[0] == '(')
        return put_struct (m, type, len, o);
    if (type[0] == 'a')
        return put_array (m, type, len, o);
    return -EPROTO;
}

int sdmsg_write (sd_bus_message *m, const char *sig, json_t *params)
{
    if (!m || !sig || !params || strlen (sig) > SIG_MAX)
        return -EPROTO;
    return put_sequence (m, sig, strlen (sig), params);
}

/* D-Bus -> JSON
 */

static int get_value (sd_bus_message *m, json_t **op);

static json_t *get_string (char type, const char *s)
{
    if (type == 'o') {
        char *path;
        json_t *o;

        if (!(path = objpath_decode (s)))
            return NULL;
        o = json_string (path);
        free (path);
        return o;
    }
    return json_string (s);
}

static int get_basic (sd_bus_message *m, char type, json_t **op)
{
    value_t v;
    json_t *o;
    int e;

    if ((e = sd_bus_message_read_basic (m, type, &v)) < 0)
        return e;
    if (e == 0)
        return -EPROTO;
    switch (type) {
        case 's':
        case 'o':
        case 'g':
            o = get_string (type, v.s);
            break;
        case 'b':
            o = json_boolean (v.b);
            break;
        case 'y':
            o = json_integer (v.y);
            break;
        case 'n':
            o = json_integer (v.n);
            break;
        case 'q':
            o = json_integer (v.q);
            break;
        case 'i':
            o = json_integer (v.i);
            break;
        case 'u':
            o = json_integer (v.u);
            break;
        case 'x': {
            char buf[32];
            snprintf (buf, sizeof (buf), "%" PRId64, v.x);
            o = json_string (buf);
            break;
        }
        case 't': {
            char buf[32];
            snprintf (buf, sizeof (buf), "%" PRIu64, v.t);
            o = json_string (buf);
            break;
        }
        case 'h':
            o = json_integer (v.h);
            break;
        case 'd':
            if (!isfinite (v.d))
                return -EPROTO;
            o = json_real (v.d);
            break;
        default:
            return -EPROTO;
    }
    if (!o)
        return -ENOMEM;
    *op = o;
    return 0;
}

/* Read values until the end of the current container, appending them
 * to JSON array 'a'.
 */
static int get_sequence (sd_bus_message *m, json_t *a)
{
    int e;

    while ((e = sd_bus_message_at_end (m, false)) == 0) {
        json_t *o;

        if ((e = get_value (m, &o)) < 0)
            return e;
        if (json_array_append_new (a, o) < 0)
            return -ENOMEM; // jansson decrefs the new object on failure
    }
    return e < 0 ? e : 0;
}

/* Read one dict entry into 'keyp' and 'valp'.
 */
static int get_dict_entry (sd_bus_message *m, json_t **keyp, json_t **valp)
{
    char type;
    const char *contents;
    json_t *key = NULL;
    json_t *val = NULL;
    int e;

    if ((e = sd_bus_message_peek_type (m, &type, &contents)) < 0)
        return e;
    if (e == 0 || type != 'e')
        return -EPROTO;
    if ((e = sd_bus_message_enter_container (m, type, contents)) < 0
        || (e = get_value (m, &key)) < 0
        || (e = get_value (m, &val)) < 0
        || (e = sd_bus_message_exit_container (m)) < 0) {
        json_decref (key);
        json_decref (val);
        return e;
    }
    *keyp = key;
    *valp = val;
    return 0;
}

/* Add 'key' and 'val' to 'dict' as an object member if the key is a
 * string, or as a [key, value] pair otherwise.  Steals both references.
 * N.B. jansson decrefs the new object on json_*_new() failure.
 */
static int dict_add (json_t *dict, json_t *key, json_t *val)
{
    int e = 0;

    if (json_is_object (dict)) {
        const char *s = json_string_value (key);

        if (json_object_get (dict, s)) {
            json_decref (val);
            e = -EPROTO; // duplicate key
        }
        else if (json_object_set_new (dict, s, val) < 0)
            e = -ENOMEM;
        json_decref (key);
    }
    else {
        json_t *pair;

        if (!(pair = json_array ())) {
            json_decref (key);
            json_decref (val);
            e = -ENOMEM;
        }
        else if (json_array_append_new (pair, key) < 0) {
            json_decref (val);
            json_decref (pair);
            e = -ENOMEM;
        }
        else if (json_array_append_new (pair, val) < 0) {
            json_decref (pair);
            e = -ENOMEM;
        }
        else if (json_array_append_new (dict, pair) < 0)
            e = -ENOMEM;
    }
    return e;
}

/* RFC 52: a dict with an s, o, or g key is a JSON object.  A dict with
 * any other key type is a JSON array of [key, value] pairs.
 * 'contents' is the dict entry type, e.g. "{sv}".
 */
static int get_dict (sd_bus_message *m, const char *contents, json_t **op)
{
    json_t *dict;
    int e;

    if (!(dict = is_string_type (contents[1]) ? json_object () : json_array ()))
        return -ENOMEM;
    while ((e = sd_bus_message_at_end (m, false)) == 0) {
        json_t *key;
        json_t *val;

        if ((e = get_dict_entry (m, &key, &val)) < 0
            || (e = dict_add (dict, key, val)) < 0)
            goto error;
    }
    if (e < 0)
        goto error;
    *op = dict;
    return 0;
error:
    json_decref (dict);
    return e;
}

static int get_value (sd_bus_message *m, json_t **op)
{
    char type;
    const char *contents;
    json_t *o = NULL;
    int e;

    if ((e = sd_bus_message_peek_type (m, &type, &contents)) < 0)
        return e;
    if (e == 0)
        return -EPROTO;
    if (is_basic (type))
        return get_basic (m, type, op);
    if ((e = sd_bus_message_enter_container (m, type, contents)) < 0)
        return e;
    switch (type) {
        case 'v': {
            json_t *val;
            if (!(o = json_pack ("[s]", contents)))
                return -ENOMEM;
            if ((e = get_value (m, &val)) < 0)
                goto error;
            if (json_array_append_new (o, val) < 0) {
                e = -ENOMEM; // jansson decrefs the new object on failure
                goto error;
            }
            break;
        }
        case 'r':
            if (!(o = json_array ()))
                return -ENOMEM;
            if ((e = get_sequence (m, o)) < 0)
                goto error;
            break;
        case 'a':
            if (contents[0] == '{') {
                if ((e = get_dict (m, contents, &o)) < 0)
                    return e;
            }
            else {
                if (!(o = json_array ()))
                    return -ENOMEM;
                if ((e = get_sequence (m, o)) < 0)
                    goto error;
            }
            break;
        default:
            return -EPROTO;
    }
    if ((e = sd_bus_message_exit_container (m)) < 0)
        goto error;
    *op = o;
    return 0;
error:
    json_decref (o);
    return e;
}

int sdmsg_read (sd_bus_message *m, json_t *params)
{
    if (!m || !json_is_array (params))
        return -EPROTO;
    return get_sequence (m, params);
}

// vi:ts=4 sw=4 expandtab
