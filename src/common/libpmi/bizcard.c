/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"

#include "bizcard.h"

struct bizcard {
    json_t *obj;
    char *encoded;
    size_t cursor;
    int refcount;
};

void bizcard_decref (struct bizcard *bc)
{
    if (bc && --bc->refcount == 0) {
        int saved_errno = errno;
        json_decref (bc->obj);
        free (bc->encoded);
        free (bc);
        errno = saved_errno;
    }
}

struct bizcard *bizcard_incref (struct bizcard *bc)
{
    if (bc)
        bc->refcount++;
    return bc;
}

struct bizcard *bizcard_create (const char *hostname, const char *pubkey)
{
    struct bizcard *bc;

    if (!hostname) {
        errno = EINVAL;
        return NULL;
    }
    if (!(bc = calloc (1, sizeof (*bc))))
        return NULL;
    bc->refcount = 1;
    if (!(bc->obj = json_pack ("{s:s s:[]}",
                               "host", hostname,
                               "uri")))
        goto nomem;
    if (pubkey) {
        json_t *o = json_string (pubkey);
        if (!o || json_object_set_new (bc->obj, "pubkey", o) < 0) {
            json_decref (o);
            goto nomem;
        }
    }
    return bc;
nomem:
    errno = ENOMEM;
    bizcard_decref (bc);
    return NULL;
}

const char *bizcard_encode (const struct bizcard *bc_const)
{
    struct bizcard *bc = (struct bizcard *)bc_const;
    if (!bc) {
        errno = EINVAL;
        return NULL;
    }
    char *s;
    if (!(s = json_dumps (bc->obj, JSON_COMPACT))) {
        errno = ENOMEM;
        return NULL;
    }
    free (bc->encoded);
    bc->encoded = s;
    return bc->encoded;
}

struct bizcard *bizcard_decode (const char *s, flux_error_t *error)
{
    struct bizcard *bc;
    json_error_t jerror;

    if (!(bc = calloc (1, sizeof (*bc))))
        return NULL;
    bc->refcount = 1;
    if (!(bc->obj = json_loads (s, 0, &jerror))) {
        errprintf (error, "%s", jerror.text);
        errno = EINVAL;
        goto error;
    }
    if (json_unpack_ex (bc->obj,
                        &jerror,
                        JSON_VALIDATE_ONLY,
                        "{s:s s?s s:o}",
                        "host",
                        "pubkey",
                        "uri") < 0) {
        errprintf (error, "%s", jerror.text);
        errno = EINVAL;
        goto error;
    }
    return bc;
error:
    bizcard_decref (bc);
    return NULL;
}

int bizcard_uri_append (struct bizcard *bc, const char *uri)
{
    json_t *a;
    json_t *o;

    if (!bc
        || !uri
        || !strstr (uri, "://")
        || !(a = json_object_get (bc->obj, "uri"))) {
        errno = EINVAL;
        return -1;
    }
    if (!(o = json_string (uri)) || json_array_append_new (a, o) < 0) {
        json_decref (o);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

const char *bizcard_uri_next (const struct bizcard *bc_const)
{
    struct bizcard *bc = (struct bizcard *)bc_const;
    json_t *a;
    json_t *o;
    const char *s;

    if (!bc
        || !(a = json_object_get (bc->obj, "uri"))
        || !(o = json_array_get (a, bc->cursor))
        || !(s = json_string_value (o)))
        return NULL;
    bc->cursor++;
    return s;
}

const char *bizcard_uri_first (const struct bizcard *bc_const)
{
    struct bizcard *bc = (struct bizcard *)bc_const;
    if (bc)
        bc->cursor = 0;
    return bizcard_uri_next (bc);
}

const char *bizcard_uri_find (const struct bizcard *bc, const char *scheme)
{
    const char *uri;

    uri = bizcard_uri_first (bc);
    while (uri) {
        if (!scheme || strstarts (uri, scheme))
            return uri;
        uri = bizcard_uri_next (bc);
    }
    return NULL;
}

const char *bizcard_pubkey (const struct bizcard *bc)
{
    const char *s;
    if (!bc || json_unpack (bc->obj, "{s:s}", "pubkey", &s) < 0)
        return NULL;
    return s;
}

const char *bizcard_hostname (const struct bizcard *bc)
{
    const char *s;
    if (!bc || json_unpack (bc->obj, "{s:s}", "host", &s) < 0)
        return NULL;
    return s;
}

// vi:ts=4 sw=4 expandtab
