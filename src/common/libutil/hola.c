/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* hola.c - hash of lists abstraciton
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <errno.h>

#include "hola.h"

struct hola {
    zhashx_t *hash; // hash items are type (zlistx_t *)
    zlistx_t *keys; // for iteration
    zlistx_destructor_fn *list_destructor;
    zlistx_duplicator_fn *list_duplicator;
    zlistx_comparator_fn *list_comparator;
    unsigned int keys_valid:1;
    unsigned int flags;
};

void hola_destroy (struct hola *hola)
{
    if (hola) {
        int saved_errno = errno;
        zlistx_destroy (&hola->keys);
        zhashx_destroy (&hola->hash);
        errno = saved_errno;
        free (hola);
    }
}

struct hola *hola_create (int flags)
{
    struct hola *hola;

    if ((flags & ~(HOLA_AUTOCREATE | HOLA_AUTODESTROY)) != 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(hola = calloc (1, sizeof (*hola))))
        return NULL;
    hola->flags = flags;
    if (!(hola->hash = zhashx_new ()))
        goto error;
    zhashx_set_destructor (hola->hash, (zhashx_destructor_fn *)zlistx_destroy);
    return hola;
error:
    hola_destroy (hola);
    return NULL;
}

void hola_set_list_destructor (struct hola *hola, zlistx_destructor_fn fun)
{
    if (hola)
        hola->list_destructor = fun;
}
void hola_set_list_duplicator (struct hola *hola, zlistx_duplicator_fn fun)
{
    if (hola)
        hola->list_duplicator = fun;
}
void hola_set_list_comparator (struct hola *hola, zlistx_comparator_fn fun)
{
    if (hola)
        hola->list_comparator = fun;
}
void hola_set_hash_key_destructor (struct hola *hola, zhashx_destructor_fn fun)
{
    if (hola)
        zhashx_set_key_destructor (hola->hash, fun);
}
void hola_set_hash_key_duplicator (struct hola *hola, zhashx_duplicator_fn fun)
{
    if (hola)
        zhashx_set_key_duplicator (hola->hash, fun);
}
void hola_set_hash_key_comparator (struct hola *hola, zhashx_comparator_fn fun)
{
    if (hola)
        zhashx_set_key_comparator (hola->hash, fun);
}
void hola_set_hash_key_hasher (struct hola *hola, zhashx_hash_fn fun)
{
    if (hola)
        zhashx_set_key_hasher (hola->hash, fun);
}

zlistx_t *hola_hash_lookup (struct hola *hola, const void *key)
{
    zlistx_t *l;

    if (!hola || !key) {
        errno = EINVAL;
        return NULL;
    }
    if (!(l = zhashx_lookup (hola->hash, key))) {
        errno = ENOENT;
        return NULL;
    }
    return l;
}

static zlistx_t *hash_add (struct hola *hola, const void *key)
{
    zlistx_t *l;

    if (!(l = zlistx_new ())) {
        errno = ENOMEM;
        return NULL;
    }
    zlistx_set_destructor (l, hola->list_destructor);
    zlistx_set_duplicator (l, hola->list_duplicator);
    zlistx_set_comparator (l, hola->list_comparator);
    if (zhashx_insert (hola->hash, key, l) < 0) {
        zlistx_destroy (&l);
        errno = EEXIST;
        return NULL;
    }
    hola->keys_valid = 0;
    return l;
}

int hola_hash_add (struct hola *hola, const void *key)
{
    if (!hola || !key) {
        errno = EINVAL;
        return -1;
    }
    if (hash_add (hola, key) == NULL)
        return -1;
    return 0;
}

int hola_hash_delete (struct hola *hola, const void *key)
{
    if (!hola || !key) {
        errno = EINVAL;
        return -1;
    }
    if (!zhashx_lookup (hola->hash, key)) {
        errno = ENOENT;
        return -1;
    }
    zhashx_delete (hola->hash, key);
    hola->keys_valid = 0;
    return 0;
}

const void *hola_hash_first (struct hola *hola)
{
    const void *key = NULL;
    if (hola) {
        if (!hola->keys_valid) {
            zlistx_destroy (&hola->keys);
            if ((hola->keys = zhashx_keys (hola->hash)))
                hola->keys_valid = 1;
        }
        if (hola->keys)
            key = zlistx_first (hola->keys);
    }
    return key;
}

const void *hola_hash_next (struct hola *hola)
{
    const void *key = NULL;
    if (hola && hola->keys) {
        key = zlistx_next (hola->keys);
    }
    return key;
}

size_t hola_hash_size (struct hola *hola)
{
    return hola ? zhashx_size (hola->hash) : 0;
}

void *hola_list_add_end (struct hola *hola, const void *key, void *item)
{
    zlistx_t *l;
    void *handle;

    if (!hola || !key || !item) {
        errno = EINVAL;
        return NULL;
    }
    if (!(l = zhashx_lookup (hola->hash, key))) {
        if ((hola->flags & HOLA_AUTOCREATE)) {
            if (!(l = hash_add (hola, key)))
                return NULL;
        }
        else {
            errno = ENOENT;
            return NULL;
        }
    }
    if ((handle = zlistx_add_end (l, item)) == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    return handle;
}

void *hola_list_insert (struct hola *hola,
                        const void *key,
                        void *item,
                        bool low_value)
{
    zlistx_t *l;
    void *handle;

    if (!hola || !key || !item) {
        errno = EINVAL;
        return NULL;
    }
    if (!(l = zhashx_lookup (hola->hash, key))) {
        if ((hola->flags & HOLA_AUTOCREATE)) {
            if (!(l = hash_add (hola, key)))
                return NULL;
        }
        else {
            errno = ENOENT;
            return NULL;
        }
    }
    if ((handle = zlistx_insert (l, item, low_value)) == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    return handle;
}

void *hola_list_find (struct hola *hola,
                      const void *key,
                      void *item)
{
    zlistx_t *l;
    void *handle;

    if (!hola || !key || !item) {
        errno = EINVAL;
        return NULL;
    }
    if (!(l = zhashx_lookup (hola->hash, key))
        || !(handle = zlistx_find (l, item))) {
        errno = ENOENT;
        return NULL;
    }
    return handle;
}

int hola_list_delete (struct hola *hola, const void *key, void *handle)
{
    zlistx_t *l;

    if (!hola || !key || !handle) {
        errno = EINVAL;
        return -1;
    }
    if (!(l = zhashx_lookup (hola->hash, key))
        || zlistx_delete (l, handle) < 0) {
        errno = ENOENT;
        return -1;
    }
    if ((hola->flags & HOLA_AUTODESTROY)) {
        if (zlistx_size (l) == 0) {
            zhashx_delete (hola->hash, key);
            hola->keys_valid = 0;
        }
    }
    return 0;
}

void *hola_list_first (struct hola *hola, const void *key)
{
    void *item = NULL;

    if (hola && key) {
        zlistx_t *l;
        if ((l = zhashx_lookup (hola->hash, key)))
            item = zlistx_first (l);
    }
    return item;
}

void *hola_list_next (struct hola *hola, const void *key)
{
    void *item = NULL;

    if (hola && key) {
        zlistx_t *l;
        if ((l = zhashx_lookup (hola->hash, key)))
            item = zlistx_next (l);
    }
    return item;
}

void *hola_list_prev (struct hola *hola, const void *key)
{
    void *item = NULL;

    if (hola && key) {
        zlistx_t *l;
        if ((l = zhashx_lookup (hola->hash, key)))
            item = zlistx_prev (l);
    }
    return item;
}

void *hola_list_last (struct hola *hola, const void *key)
{
    void *item = NULL;

    if (hola && key) {
        zlistx_t *l;
        if ((l = zhashx_lookup (hola->hash, key)))
            item = zlistx_last (l);
    }
    return item;
}


void *hola_list_cursor (struct hola *hola, const void *key)
{
    void *handle = NULL;

    if (hola && key) {
        zlistx_t *l;
        if ((l = zhashx_lookup (hola->hash, key)))
            handle = zlistx_cursor (l);
    }
    return handle;
}

size_t hola_list_size (struct hola *hola, const void *key)
{
    size_t size = 0;

    if (hola && key) {
        zlistx_t *l;
        if ((l = zhashx_lookup (hola->hash, key)))
            size = zlistx_size (l);
    }
    return size;
}


// vi:ts=4 sw=4 expandtab
