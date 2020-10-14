/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* rutil.c - random standalone helper functions */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libidset/idset.h"
#include "src/common/libutil/errno_safe.h"

#include "rutil.h"

int rutil_idset_sub (struct idset *ids1, const struct idset *ids2)
{
    if (!ids1) {
        errno = EINVAL;
        return -1;
    }
    if (ids2) {
        unsigned int id;
        id = idset_first (ids2);
        while (id != IDSET_INVALID_ID) {
            if (idset_clear (ids1, id) < 0)
                return -1;
            id = idset_next (ids2, id);
        }
    }
    return 0;
}

int rutil_idset_add (struct idset *ids1, const struct idset *ids2)
{
    if (!ids1) {
        errno = EINVAL;
        return -1;
    }
    if (ids2) {
        unsigned int id;
        id = idset_first (ids2);
        while (id != IDSET_INVALID_ID) {
            if (idset_set (ids1, id) < 0)
                return -1;
            id = idset_next (ids2, id);
        }
    }
    return 0;
}

int rutil_idset_diff (const struct idset *ids1,
                      const struct idset *ids2,
                      struct idset **addp,
                      struct idset **subp)
{
    struct idset *add = NULL;
    struct idset *sub = NULL;
    unsigned int id;

    if (!addp || !subp) {
        errno = EINVAL;
        return -1;
    }
    if (ids1) { // find ids in ids1 but not in ids2, and add to 'sub'
        id = idset_first (ids1);
        while (id != IDSET_INVALID_ID) {
            if (!ids2 || !idset_test (ids2, id)) {
                if (!sub && !(sub = idset_create (0, IDSET_FLAG_AUTOGROW)))
                    goto error;
                if (idset_set (sub, id) < 0)
                    goto error;
            }
            id = idset_next (ids1, id);
        }
    }
    if (ids2) { // find ids in ids2 but not in ids1, and add to 'add'
        id = idset_first (ids2);
        while (id != IDSET_INVALID_ID) {
            if (!ids1 || !idset_test (ids1, id)) {
                if (!add && !(add = idset_create (0, IDSET_FLAG_AUTOGROW)))
                    goto error;
                if (idset_set (add, id) < 0)
                    goto error;
            }
            id = idset_next (ids2, id);
        }
    }
    *addp = add;
    *subp = sub;
    return 0;
error:
    idset_destroy (add);
    idset_destroy (sub);
    return -1;
}

int rutil_set_json_idset (json_t *o, const char *key, const struct idset *ids)
{
    json_t *val = NULL;
    char *s = NULL;

    if (o == NULL || key == NULL || strlen (key) == 0) {
        errno = EINVAL;
        return -1;
    }
    if (ids && !(s = idset_encode (ids, IDSET_FLAG_RANGE)))
        return -1;
    if (!(val = json_string (s ? s : "")))
        goto nomem;
    if (json_object_set_new (o, key, val) < 0)
        goto nomem;
    free (s);
    return 0;
nomem:
    errno = ENOMEM;
    ERRNO_SAFE_WRAP (free, s);
    ERRNO_SAFE_WRAP (json_decref, val);
    return -1;
}

struct idset *rutil_idset_from_resobj (const json_t *resobj)
{
    struct idset *ids;
    const char *key;
    json_t *val;

    if (!(ids = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return NULL;
    if (resobj) {
        json_object_foreach ((json_t *)resobj, key, val) {
            struct idset *valset;
            unsigned long id;

            if (!(valset = idset_decode (key)))
                goto error;
            id = idset_first (valset);
            while (id != IDSET_INVALID_ID) {
                if (idset_set (ids, id) < 0) {
                    idset_destroy (valset);
                    goto error;
                }
                id = idset_next (valset, id);
            }
            idset_destroy (valset);
        }
    }
    return ids;
error:
    idset_destroy (ids);
    return NULL;
}

json_t *rutil_resobj_sub (const json_t *resobj, const struct idset *ids)
{
    const char *key;
    json_t *val;
    json_t *resobj2;

    if (!resobj) {
        errno = EINVAL;
        return NULL;
    }
    if (!(resobj2 = json_object ())) {
        errno = ENOMEM;
        return NULL;
    }
    json_object_foreach ((json_t *)resobj, key, val) {
        struct idset *valset;
        char *key2;

        if (!(valset = idset_decode (key)))
            goto error;
        if (rutil_idset_sub (valset, ids) < 0)
            goto error;
        if (idset_count (valset) > 0) {
            if (!(key2 = idset_encode (valset, IDSET_FLAG_RANGE))) {
                idset_destroy (valset);
                goto error;
            }
            if (json_object_set (resobj2, key2, val) < 0) {
                idset_destroy (valset);
                ERRNO_SAFE_WRAP (free, key2);
                goto error;
            }
            free (key2);
        }
        idset_destroy (valset);
    }
    return resobj2;
error:
    ERRNO_SAFE_WRAP (json_decref, resobj2);
    return NULL;
}

bool rutil_idset_decode_test (const char *idset, unsigned long id)
{
    struct idset *ids;
    bool result;

    if (!(ids = idset_decode (idset)))
        return false;
    result = idset_test (ids, id);
    idset_destroy (ids);
    return result;
}

bool rutil_match_request_sender (const flux_msg_t *msg1,
                                 const flux_msg_t *msg2)
{
    char *sender1 = NULL;
    char *sender2 = NULL;
    bool match = false;

    if (!msg1 || !msg2)
        goto done;
    if (flux_msg_get_route_first (msg1, &sender1) < 0)
        goto done;
    if (flux_msg_get_route_first (msg2, &sender2) < 0)
        goto done;
    if (!sender1 || !sender2)
        goto done;
    if (!strcmp (sender1, sender2))
        match = true;
done:
    free (sender1);
    free (sender2);
    return match;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
