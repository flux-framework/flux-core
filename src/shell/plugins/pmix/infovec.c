/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* infovec.c - helper class for working with pmix_info_t arrays
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <jansson.h>
#include <pmix_server.h>

#include "infovec.h"
#include "codec.h"

#define INFOVEC_CHUNK 8

struct infovec {
    int length;
    int count;
    pmix_info_t *info;
};

int pp_infovec_count (struct infovec *iv)
{
    return iv->count;
}

pmix_info_t *pp_infovec_info (struct infovec *iv)
{
    return iv->info;
}

void pp_infovec_destroy (struct infovec *iv)
{
    if (iv) {
        int saved_errno = errno;
        for (int i = 0; i < iv->count; i++)
            pp_info_release (&iv->info[i]);
        free (iv);
        errno = saved_errno;
    }
}

static pmix_info_t *alloc_slot (struct infovec *iv)
{
    if (iv->count == iv->length) {
        int new_length = iv->length + INFOVEC_CHUNK;
        size_t new_size = sizeof (iv->info[0]) * new_length;
        pmix_info_t *new_info;

        if (!(new_info = realloc (iv->info, new_size)))
            return NULL;
        iv->info = new_info;
        iv->length = new_length;
    }
    return &iv->info[iv->count++];
}

int pp_infovec_set_str (struct infovec *iv, const char *key, const char *val)
{
    char *cpy;
    pmix_info_t *info;

    if (!(cpy = strdup (val))
        || !(info = alloc_slot (iv))) {
        free (cpy);
        return -1;
    }
    strncpy (info->key, key, PMIX_MAX_KEYLEN);
    info->value.type = PMIX_STRING;
    info->value.data.string = cpy;
    return 0;
}

int pp_infovec_set_u32 (struct infovec *iv, const char *key, uint32_t value)
{
    pmix_info_t *info;

    if (!(info = alloc_slot (iv)))
        return -1;
    strncpy (info->key, key, PMIX_MAX_KEYLEN);
    info->value.type = PMIX_UINT32;
    info->value.data.uint32 = value;
    return 0;
}

struct infovec *pp_infovec_create (void)
{
    struct infovec *iv;
    if (!(iv = calloc (1, sizeof (*iv))))
        return NULL;
    return iv;
}

struct infovec *pp_infovec_create_from_json (json_t *o)
{
    size_t index;
    json_t *value;
    struct infovec *iv;
    pmix_info_t *info;

    if ( !json_is_array (o)
        || !(iv = pp_infovec_create ()))
        return NULL;
    json_array_foreach (o, index, value) {
        if (!(info = alloc_slot (iv))
            || pp_info_decode (value, info) < 0)
            goto error;
    }
    return iv;
error:
    pp_infovec_destroy (iv);
    return NULL;
}

// vi:tabstop=4 shiftwidth=4 expandtab
