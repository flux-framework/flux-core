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
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <errno.h>
#include <systemd/sd-bus.h>
#include <assert.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"
#include "ccan/array_size/array_size.h"

#include "objpath.h"
#include "message.h"

typedef union {
    uint8_t u8;
    int16_t i16;
    uint16_t u16;
    int32_t i32;
    uint32_t u32;
    int64_t i64;
    uint64_t u64;
    int i;
    double f;
} number_t;

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

static int sdmsg_put_array (sd_bus_message *m, const char *fmt, json_t *o)
{
    int e;
    size_t index;
    json_t *entry;

    if ((e = sd_bus_message_open_container (m, 'a', fmt)) < 0)
        return e;
    json_array_foreach (o, index, entry) {
        if ((e = sdmsg_put (m, fmt, entry)) < 0)
            return e;
    }
    if ((e = sd_bus_message_close_container (m)) < 0)
        return e;

    return e;
}

static int sdmsg_put_string (sd_bus_message *m, char type, json_t *o)
{
    int e;
    switch (type) {
        case 'g':
        case 's': {
            const char *val = json_string_value (o);
            if (!val)
                return -EPROTO;
            if ((e = sd_bus_message_append_basic (m, type, val)) < 0)
                return e;
            break;
        }
        case 'o': {
            char *val = objpath_encode (json_string_value (o));
            if (!val)
                return -errno;
            e = sd_bus_message_append_basic (m, type, val);
            free (val);
            if (e < 0)
                return e;
            break;
        }
        default:
            return -EPROTO;
    }
    return 0;
}

static int sdmsg_put_basic (sd_bus_message *m, char type, json_t *o)
{
    number_t n;

    if (!m || !o)
        return -EPROTO;
    if (type == 's' || type == 'g' || type == 'o')
        return sdmsg_put_string (m, type, o);
    switch (type) {
        case 'y':
            n.u8 = json_integer_value (o);
            break;
        case 'b':
            n.i = json_is_true (o) ? 1 : 0;
            break;
        case 'n':
            n.i16 = json_integer_value (o);
            break;
        case 'q':
            n.u16 = json_integer_value (o);
            break;
        case 'i':
            n.i32 = json_integer_value (o);
            break;
        case 'u':
            n.u32 = json_integer_value (o);
            break;
        case 'x':
            n.i64 = json_integer_value (o);
            break;
        case 't':
            n.u64 = json_integer_value (o);
            break;
        case 'h':
            n.i = json_integer_value (o);
            break;
        case 'd':
            n.f = json_real_value (o);
            break;
        default:
            return -EPROTO;
    }
    return sd_bus_message_append_basic (m, type, &n);
}

static int sdmsg_put_variant (sd_bus_message *m, json_t *o)
{
    const char *type;
    json_t *val;
    int e;

    if (json_unpack (o, "[so]", &type, &val) < 0)
        return -EPROTO;
    if ((e = sd_bus_message_open_container (m, 'v', type)) < 0)
        return e;
    if ((e = sdmsg_put (m, type, val)) < 0)
        return e;
    if ((e = sd_bus_message_close_container (m)) < 0)
        return e;
    return 0;
}

static int sdmsg_put_struct (sd_bus_message *m, const char *fmt, json_t *o)
{
    int e;

    if ((e = sd_bus_message_open_container (m, 'r', fmt)) < 0)
        return e;
    if ((e = sdmsg_write (m, fmt, o)) < 0)
        return e;
    if ((e = sd_bus_message_close_container (m)) < 0)
        return e;
    return 0;
}

int sdmsg_put (sd_bus_message *m, const char *fmt, json_t *o)
{
    int e;

    if (streq (fmt, "a(sv)"))
        e = sdmsg_put_array (m, "(sv)", o);
    else if (streq (fmt, "a(sasb)"))
        e = sdmsg_put_array (m, "(sasb)", o);
    else if (fmt[0] == 'a' && strlen (fmt) == 2)
        e = sdmsg_put_array (m, &fmt[1], o);
    else if (streq (fmt, "(sv)"))
        e = sdmsg_put_struct (m, "sv", o);
    else if (streq (fmt, "(sasb)"))
        e = sdmsg_put_struct (m, "sasb", o);
    else if (streq (fmt, "v"))
        e = sdmsg_put_variant (m, o);
    else if (strlen (fmt) == 1)
        e = sdmsg_put_basic (m, fmt[0], o);
    else
        e = -EPROTO;
    return e;
}

int sdmsg_write (sd_bus_message *m, const char *fmt, json_t *o)
{
    int e;

    if (!json_is_array (o))
        return -EPROTO;

    int cursor = 0;
    for (int i = 0; fmt[i] != '\0';) {
        json_t *entry;
        char *efmt = NULL;

        if (!(entry = json_array_get (o, cursor++)))
            return -EPROTO;
        if (strstarts (&fmt[i], "a(sasb)"))
            efmt = strndup (&fmt[i], 7);
        else if (strstarts (&fmt[i], "a(sv)"))
            efmt = strndup (&fmt[i], 5);
        else if (fmt[i] == 'a' && strlen (&fmt[i]) > 1)
            efmt = strndup (&fmt[i], 2);
        else
            efmt = strndup (&fmt[i], 1);
        if (!efmt)
            return -ENOMEM;
        if ((e = sdmsg_put (m, efmt, entry)) < 0) {
            free (efmt);
            return e;
        }
        i += strlen (efmt);
        free (efmt);
    }
    return 0;
}

static int sdmsg_get_string (sd_bus_message *m, char type, json_t **op)
{
    int e;
    const char *val = NULL;
    json_t *o = NULL;

    if ((e = sd_bus_message_read_basic (m, type, &val)) <= 0)
        return e;
    switch (type) {
        case 'g':
        case 's':
            o = json_string (val);
            break;
        case 'o': {
            char *tmp;
            if (!(tmp = objpath_decode (val)))
                return -EPROTO;
            o = json_string (tmp);
            free (tmp);
            break;
        }
        default:
            return -EPROTO;
    }
    if (!o)
        return -ENOMEM;
    *op = o;
    return 1;
}

static int sdmsg_get_basic (sd_bus_message *m, char type, json_t **op)
{
    char peek_type;
    int e;
    json_t *o;
    number_t n;

    if ((e = sd_bus_message_peek_type (m, &peek_type, NULL)) <= 0)
        return e;
    if (type == 0)
        type = peek_type;
    if (type != peek_type)
        return -EPROTO;
    if (type == 'g' || type == 's' || type == 'o')
        return sdmsg_get_string (m, type, op);
    if ((e = sd_bus_message_read_basic (m, type, &n)) <= 0)
        return e;
    switch (type) {
        case 'y':
            o = json_integer (n.u8);
            break;
        case 'n':
            o = json_integer (n.i16);
            break;
        case 'q':
            o = json_integer (n.u16);
            break;
        case 'i':
            o = json_integer (n.i32);
            break;
        case 'u':
            o = json_integer (n.u32);
            break;
        case 'x':
            o = json_integer (n.i64);
            break;
        case 't':
            o = json_integer (n.u64);
            break;
        case 'b':
            o = n.i ? json_true () : json_false ();
            break;
        case 'h':
            o = json_integer (n.i);
            break;
        case 'd':
            o = json_real (n.f);
            break;
        default:
            return -EPROTO;
    }
    if (!o)
        return -ENOMEM;
    *op = o;
    return 1;
}

static int sdmsg_get_array (sd_bus_message *m, const char *fmt, json_t **op)
{
    json_t *a;
    int e;

    if (!(a = json_array ()))
        return -ENOMEM;
    if ((e = sd_bus_message_enter_container (m, 'a', fmt)) <= 0) {
        json_decref (a);
        return e;
    }
    while ((e = sdmsg_read (m, fmt, a)) > 0)
        ;
    if (e < 0) {
        json_decref (a);
        return e;
    }
    if ((e = sd_bus_message_exit_container (m)) < 0) {
        json_decref (a);
        return e;
    }
    *op = a;
    return 1;
}

static int sdmsg_get_unknown (sd_bus_message *m, const char *fmt, json_t **op)
{
    int e;
    if ((e = sd_bus_message_skip (m, fmt)) <= 0)
        return e;
    *op = json_null ();
    return e;
}

static int sdmsg_get_variant (sd_bus_message *m, json_t **op)
{
    const char *contents;
    char type;
    json_t *val = NULL;
    json_t *o = NULL;
    int e;

    if ((e = sd_bus_message_peek_type (m, &type, &contents)) <= 0)
        return e;
    if (type != 'v')
        return -EPROTO;
    if ((e = sd_bus_message_enter_container (m, 'v', contents)) <= 0)
        return e;
    if (strlen (contents) == 1)
        e = sdmsg_get_basic (m, contents[0], &val);
    else if (strlen (contents) == 2 && contents[0] == 'a')
        e = sdmsg_get_array (m, contents + 1, &val);
    else
        e = sdmsg_get_unknown (m, contents, &val);
    if (e <= 0)
        return e;
    if (!(o = json_pack ("[sO]", contents, val))) {
        json_decref (val);
        return -ENOMEM;
    }
    if ((e = sd_bus_message_exit_container (m)) < 0) {
        json_decref (val);
        json_decref (o);
        return e;
    }
    json_decref (val);
    *op = o;
    return 1;
}

static int sdmsg_get_property_dict (sd_bus_message *m, json_t **op)
{
    json_t *dict;
    int e;

    if (!(dict = json_object ()))
        return -ENOMEM;
    if ((e = sd_bus_message_enter_container (m, 'a', "{sv}")) <= 0)
        goto out;
    while ((e = sd_bus_message_enter_container (m, 'e', "sv")) > 0) {
        const char *key;
        json_t *val;
        int e;
        if ((e = sd_bus_message_read (m, "s", &key)) <= 0
            || (e = sdmsg_get_variant (m, &val)) <= 0)
            goto out;
        if (json_object_set_new (dict, key, val) < 0) {
            json_decref (val);
            goto nomem;
        }
        if ((e = sd_bus_message_exit_container (m)) < 0)
            goto out;
    }
    if (e < 0 || (e = sd_bus_message_exit_container (m)) < 0)
        goto out;
    *op = dict;
    return 1;
nomem:
    e = -ENOMEM;
out:
    json_decref (dict);
    return e;
}

int sdmsg_get (sd_bus_message *m, const char *fmt, json_t **op)
{
    int e;

    if (streq (fmt, "a{sv}"))
        e = sdmsg_get_property_dict (m, op);
    else if (fmt[0] == 'a' && strlen (fmt) > 1)
        e = sdmsg_get_array (m, fmt + 1, op);
    else if (streq (fmt, "v"))
        e = sdmsg_get_variant (m, op);
    else if (strlen (fmt) == 1)
        e = sdmsg_get_basic (m, fmt[0], op);
    else
        e = -EPROTO;
    return e;
}

int sdmsg_read (sd_bus_message *m, const char *fmt, json_t *o)
{
    for (int i = 0; fmt[i] != '\0';) {
        json_t *entry = NULL;
        char *efmt = NULL;
        int e;

        if (strstarts (&fmt[i], "a{sv}"))
            efmt = strndup (&fmt[i], 5);
        else if (fmt[i] == 'a' && strlen (&fmt[i]) > 1)
            efmt = strndup (&fmt[i], 2);
        else
            efmt = strndup (&fmt[i], 1);
        if (!efmt)
            return -ENOMEM;

        if ((e = sdmsg_get (m, efmt, &entry)) <= 0) {
            free (efmt);
            if (e == 0 && i > 0)
                e = -EPROTO;
            return e;
        }
        if (json_array_append_new (o, entry) < 0) {
            free (efmt);
            json_decref (entry);
            return -ENOMEM;
        }
        i += strlen (efmt);
        free (efmt);
    }
    return 1;
}

// vi:ts=4 sw=4 expandtab
