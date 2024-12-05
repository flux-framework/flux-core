/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* See RFC 11 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <jansson.h>

#include "ccan/base64/base64.h"
#include "ccan/str/str.h"
#include "src/common/libutil/blobref.h"

#include "treeobj.h"

static const int treeobj_version = 1;

static int treeobj_unpack (json_t *obj, const char **typep, json_t **datap)
{
    json_t *data;
    int version;
    const char *type;
    if (!obj
        || json_unpack (obj,
                        "{s:i s:s s:o !}",
                        "ver", &version,
                        "type", &type,
                        "data", &data) < 0
        || version != treeobj_version) {
        errno = EINVAL;
        return -1;
    }
    if (typep)
        *typep = type;
    if (datap)
        *datap = data;
    return 0;
}

static int treeobj_peek (const json_t *obj,
                         const char **typep,
                         const json_t **datap)
{
    json_t *data;
    int version;
    const char *type;

    /* N.B. it should be safe to cast away const on 'obj' as long as 'data'
     * parameter is not modified.  We make 'data' const to ensure that.
     */
    if (!obj
        || json_unpack ((json_t *)obj,
                        "{s:i s:s s:o !}",
                        "ver", &version,
                        "type", &type,
                        "data", &data) < 0
        || version != treeobj_version) {
        errno = EINVAL;
        return -1;
    }
    if (typep)
        *typep = type;
    if (datap)
        *datap = data;
    return 0;
}

int treeobj_validate (const json_t *obj)
{
    const json_t *o;
    const json_t *data;
    const char *type;

    if (treeobj_peek (obj, &type, &data) < 0)
        goto inval;
    if (streq (type, "valref") || streq (type, "dirref")) {
        int i, len;
        if (!json_is_array (data))
            goto inval;
        len = json_array_size (data);
        if (len == 0)
            goto inval;
        json_array_foreach (data, i, o) {
            if (blobref_validate (json_string_value (o)) < 0)
                goto inval;
        }
    }
    else if (streq (type, "dir")) {
        const char *key;
        if (!json_is_object (data))
            goto inval;
        /* N.B. it should be safe to cast away const on 'data' as long as
         * 'o' is not modified.  We make 'o' const to ensure that.
         */
        json_object_foreach ((json_t *)data, key, o) {
            if (treeobj_validate (o) < 0)
                goto inval;
        }
    }
    else if (streq (type, "symlink")) {
        json_t *o;
        if (!json_is_object (data))
            goto inval;
        if (!(o = json_object_get (data, "target")))
            goto inval;
        if (!json_is_string (o))
            goto inval;
        /* namespace is optional, not an error if doesn't exist */
        if ((o = json_object_get (data, "namespace"))) {
            if (!json_is_string (o))
                goto inval;
        }
    }
    else if (streq (type, "val")) {
        /* is base64, should always be a string */
        if (!json_is_string (data))
            goto inval;
    }
    else
        goto inval;
    return 0;
inval:
    errno = EINVAL;
    return -1;
}

const char *treeobj_get_type (const json_t *obj)
{
    const char *type;
    if (!obj || treeobj_peek (obj, &type, NULL) < 0)
        return NULL;
    return type;
}

bool treeobj_is_symlink (const json_t *obj)
{
    const char *type = treeobj_get_type (obj);
    return type && streq (type, "symlink");
}

bool treeobj_is_val (const json_t *obj)
{
    const char *type = treeobj_get_type (obj);
    return type && streq (type, "val");
}

bool treeobj_is_valref (const json_t *obj)
{
    const char *type = treeobj_get_type (obj);
    return type && streq (type, "valref");
}

bool treeobj_is_dir (const json_t *obj)
{
    const char *type = treeobj_get_type (obj);
    return type && streq (type, "dir");
}

bool treeobj_is_dirref (const json_t *obj)
{
    const char *type = treeobj_get_type (obj);
    return type && streq (type, "dirref");
}

json_t *treeobj_get_data (json_t *obj)
{
    json_t *data;
    if (treeobj_unpack (obj, NULL, &data) < 0)
        return NULL;
    return data;
}

int treeobj_get_symlink (const json_t *obj,
                         const char **ns,
                         const char **target)
{
    const char *type;
    const json_t *data;
    const json_t *n, *t;
    const char *n_str = NULL, *t_str = NULL;

    if (treeobj_peek (obj, &type, &data) < 0
            || !streq (type, "symlink")) {
        errno = EINVAL;
        return -1;
    }
    /* namespace is optional, not an error if it doesn't exist */
    if ((n = json_object_get (data, "namespace"))) {
        if (!(n_str = json_string_value (n))) {
            errno = EINVAL;
            return -1;
        }
    }
    if (!(t = json_object_get (data, "target"))) {
        errno = EINVAL;
        return -1;
    }
    if (!(t_str = json_string_value (t))) {
        errno = EINVAL;
        return -1;
    }
    if (ns)
        (*ns) = n_str;
    if (target)
        (*target) = t_str;
    return 0;
}

int treeobj_decode_val (const json_t *obj, void **dp, int *lp)
{
    const char *type, *xdatastr;
    const json_t *xdata;
    size_t databuflen, xlen;
    ssize_t datalen;
    char *data;

    if (treeobj_peek (obj, &type, &xdata) < 0
            || !streq (type, "val")) {
        errno = EINVAL;
        return -1;
    }
    xdatastr = json_string_value (xdata);
    xlen = strlen (xdatastr);
    databuflen = base64_decoded_length (xlen) + 1; // +1  for a trailing \0

    if (databuflen > 1) {
        if (!(data = malloc (databuflen)))
            return -1;
        if ((datalen = base64_decode (data, databuflen, xdatastr, xlen)) < 0) {
            free (data);
            errno = EINVAL;
            return -1;
        }
    }
    else {
        data = NULL;
        datalen = 0;
    }
    if (lp)
        *lp = datalen;
    if (dp)
        *dp = data;
    else
        free (data);
    return 0;
}

int treeobj_get_count (const json_t *obj)
{
    const json_t *data;
    const char *type;
    int count = -1;

    if (treeobj_peek (obj, &type, &data) < 0)
        goto done;
    if (streq (type, "valref") || streq (type, "dirref")) {
        count = json_array_size (data);
    }
    else if (streq (type, "dir")) {
        count = json_object_size (data);
    }
    else if (streq (type, "symlink") || streq (type, "val")) {
        count = 1;
    } else {
        errno = EINVAL; // unknown type
        return -1;
    }
done:
    return count;
}

json_t *treeobj_get_entry (json_t *obj, const char *name)
{
    const char *type;
    json_t *data, *obj2;

    if (treeobj_unpack (obj, &type, &data) < 0
        || !streq (type, "dir")) {
        errno = EINVAL;
        return NULL;
    }
    if (!(obj2 = json_object_get (data, name))) {
        errno = ENOENT;
        return NULL;
    }
    return obj2;
}

int treeobj_delete_entry (json_t *obj, const char *name)
{
    const char *type;
    json_t *data;

    if (treeobj_unpack (obj, &type, &data) < 0
        || !streq (type, "dir")) {
        errno = EINVAL;
        return -1;
    }
    if (json_object_del (data, name) < 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int treeobj_insert_entry (json_t *obj, const char *name, json_t *obj2)
{
    const char *type;
    json_t *data;

    if (!name
        || !obj2
        || treeobj_unpack (obj, &type, &data) < 0
        || !streq (type, "dir")
        || treeobj_validate (obj2) < 0) {
        errno = EINVAL;
        return -1;
    }
    if (json_object_set (data, name, obj2) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

/* identical to treeobj_insert_entry() but does not call
 * treeobj_validate() */
int treeobj_insert_entry_novalidate (json_t *obj,
                                     const char *name,
                                     json_t *obj2)
{
    const char *type;
    json_t *data;

    if (!name
        || !obj2 || treeobj_unpack (obj, &type, &data) < 0
        || !streq (type, "dir")
        || treeobj_peek (obj2, NULL, NULL) < 0) {
        errno = EINVAL;
        return -1;
    }
    if (json_object_set (data, name, obj2) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

const json_t *treeobj_peek_entry (const json_t *obj, const char *name)
{
    const char *type;
    const json_t *data, *obj2;

    if (treeobj_peek (obj, &type, &data) < 0
        || !streq (type, "dir")) {
        errno = EINVAL;
        return NULL;
    }
    if (!(obj2 = json_object_get (data, name))) {
        errno = ENOENT;
        return NULL;
    }
    return obj2;
}

json_t *treeobj_copy (json_t *obj)
{
    json_t *data;
    json_t *cpy;
    json_t *datacpy;
    int save_errno;

    if (treeobj_unpack (obj, NULL, &data) < 0) {
        errno = EINVAL;
        return NULL;
    }
    /* shallow copy of treeobj data and deep copy of treeobj is
     * identical except for dir object.
     */
    if (treeobj_is_dir (obj)) {
        if (!(cpy = treeobj_create_dir ()))
            return NULL;

        if (!(datacpy = json_copy (data))) {
            save_errno = errno;
            json_decref (cpy);
            errno = save_errno;
            return NULL;
        }
        if (json_object_set_new (cpy, "data", datacpy) < 0) {
            save_errno = errno;
            json_decref (datacpy);
            json_decref (cpy);
            errno = save_errno;
            return NULL;
        }
    }
    else {
        if (!(cpy = json_deep_copy (obj)))
            return NULL;
    }
    return cpy;
}

json_t *treeobj_deep_copy (const json_t *obj)
{
    return json_deep_copy (obj);
}

int treeobj_append_blobref (json_t *obj, const char *blobref)
{
    const char *type;
    json_t *data, *o;
    int rc = -1;

    if (!blobref
        || blobref_validate (blobref) < 0
        || treeobj_unpack (obj, &type, &data) < 0
        || (!streq (type, "dirref") && !streq (type, "valref"))) {
        errno = EINVAL;
        goto done;
    }
    if (!(o = json_string (blobref))) {
        errno = ENOMEM;
        goto done;
    }
    if (json_array_append_new (data, o) < 0) {
        json_decref (o);
        errno = ENOMEM;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

const char *treeobj_get_blobref (const json_t *obj, int index)
{
    const json_t *data;
    json_t *o;
    const char *type, *blobref = NULL;

    if (treeobj_peek (obj, &type, &data) < 0
        || (!streq (type, "dirref") && !streq (type, "valref"))) {
        errno = EINVAL;
        goto done;
    }
    if (!(o = json_array_get (data, index))) {
        errno = EINVAL;
        goto done;
    }
    blobref = json_string_value (o);
done:
    return blobref;
}

json_t *treeobj_create_dir (void)
{
    json_t *obj;

    if (!(obj = json_pack ("{s:i s:s s:{}}",
                           "ver", treeobj_version,
                           "type", "dir",
                           "data"))) {
        errno = ENOMEM;
        return NULL;
    }
    return obj;
}

json_t *treeobj_create_symlink (const char *ns, const char *target)
{
    json_t *data, *obj;

    if (!target) {
        errno = EINVAL;
        return NULL;
    }

    if (!ns) {
        if (!(data = json_pack ("{s:s}", "target", target))) {
            errno = ENOMEM;
            return NULL;
        }
    }
    else {
        if (!(data = json_pack ("{s:s s:s}",
                                "namespace", ns,
                                "target", target))) {
            errno = ENOMEM;
            return NULL;
        }
    }

    /* obj takes reference to "data" */
    if (!(obj = json_pack ("{s:i s:s s:o}",
                           "ver", treeobj_version,
                           "type", "symlink",
                           "data", data))) {
        json_decref (data);
        errno = ENOMEM;
        return NULL;
    }

    return obj;
}

json_t *treeobj_create_val (const void *data, int len)
{
    int xlen;
    char *xdata;
    json_t *obj = NULL;

    xlen = base64_encoded_length (len) + 1; /* +1 for NUL */
    if (!(xdata = malloc (xlen)))
        goto done;
    if (base64_encode (xdata, xlen, (char *)data, len) < 0)
        goto done;
    if (!(obj = json_pack ("{s:i s:s s:s}",
                           "ver", treeobj_version,
                           "type", "val",
                           "data", xdata))) {
        errno = ENOMEM;
        goto done;
    }
done:
    free (xdata);
    return obj;
}

json_t *treeobj_create_valref (const char *blobref)
{
    json_t *obj;

    if (blobref) {
        obj = json_pack ("{s:i s:s s:[s]}",
                         "ver", treeobj_version,
                         "type", "valref",
                         "data", blobref);
    }
    else {
        obj = json_pack ("{s:i s:s s:[]}",
                         "ver", treeobj_version,
                         "type", "valref",
                         "data");
    }
    if (!obj) {
        errno = ENOMEM;
        return NULL;
    }
    return obj;
}

json_t *treeobj_create_dirref (const char *blobref)
{
    json_t *obj;

    if (blobref) {
        obj = json_pack ("{s:i s:s s:[s]}",
                         "ver", treeobj_version,
                         "type", "dirref",
                         "data", blobref);
    }
    else {
        obj = json_pack ("{s:i s:s s:[]}",
                         "ver", treeobj_version,
                         "type", "dirref",
                         "data");
    }
    if (!obj) {
        errno = ENOMEM;
        return NULL;
    }
    return obj;
}

json_t *treeobj_create_valref_buf (const char *hashtype,
                                   int maxblob,
                                   void *data,
                                   int len)
{
    json_t *valref = NULL;
    char blobref[BLOBREF_MAX_STRING_SIZE];
    int blob_len;

    if (!(valref = treeobj_create_valref (NULL)))
        goto error;
    while (len >= 0) { // N.B. handle zero-length blob
        blob_len = len;
        if (maxblob > 0 && len > maxblob)
            blob_len = maxblob;
        if (blobref_hash (hashtype,
                          data,
                          blob_len,
                          blobref,
                          sizeof (blobref)) < 0)
            goto error;
        if (treeobj_append_blobref (valref, blobref) < 0)
            goto error;
        len -= blob_len;
        data += blob_len;
        if (len == 0)
            break;
    }
    return valref;
error:
    json_decref (valref);
    return NULL;
}

json_t *treeobj_decode (const char *buf)
{
    if (!buf) {
        errno = EINVAL;
        return NULL;
    }
    return treeobj_decodeb (buf, strlen (buf));
}

json_t *treeobj_decodeb (const char *buf, size_t buflen)
{
    json_t *obj = NULL;
    if (!(obj = json_loadb (buf, buflen, 0, NULL))
        || treeobj_validate (obj) < 0) {
        errno = EPROTO;
        goto error;
    }
    return obj;
error:
    json_decref (obj);
    return NULL;
}

char *treeobj_encode (const json_t *obj)
{
    return json_dumps (obj, JSON_COMPACT|JSON_SORT_KEYS);
}

const char *treeobj_type_name (const json_t *obj)
{
    if (treeobj_is_symlink (obj))
        return "symlink";
    else if (treeobj_is_val (obj))
        return "val";
    else if (treeobj_is_valref (obj))
        return "valref";
    else if (treeobj_is_dir (obj))
        return "dir";
    else if (treeobj_is_dirref (obj))
        return "dirref";
    return "unknown";
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
