/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "msg_hash.h"

/* get message matchtag, or if there is none, return FLUX_MATCHTAG_NONE */
static uint32_t msg_hash_matchtag_get (const flux_msg_t *msg)
{
    uint32_t matchtag = FLUX_MATCHTAG_NONE;
    (void)flux_msg_get_matchtag (msg, &matchtag);
    return matchtag;
}
/* get request sender uuid, or if there is none, return empty string */
static const char *msg_hash_uuid_get (const flux_msg_t *msg)
{
    const char *uuid = flux_msg_route_first (msg);
    return uuid ? uuid : "";
}

/* Use "modified Bernstein hash" as employed by zhashx internally, but input
 * is message uuid+matchtag instead of a simple NULL-terminated string.
 * N.B. zhashx_hash_fn signature
 */
static size_t msg_hash_uuid_matchtag_hasher (const void *key)
{
    size_t key_hash = 0;
    const char *cp;
    uint32_t matchtag = msg_hash_matchtag_get (key);
    int i;

    cp = msg_hash_uuid_get (key);
    while (*cp)
        key_hash = 33 * key_hash ^ *cp++;
    cp = (const char *)&matchtag;
    for (i = 0; i < sizeof (matchtag); i++)
        key_hash = 33 * key_hash ^ *cp++;
    return key_hash;
}

#define NUMCMP(a,b) ((a)==(b)?0:((a)<(b)?-1:1))

/* Compare hash keys, consisting of message uuid+matchtag.
 * N.B. zhashx_comparator_fn signature
 */
static int msg_hash_uuid_matchtag_key_cmp (const void *key1, const void *key2)
{
    int ret;

    ret = strcmp (msg_hash_uuid_get (key1),
                  msg_hash_uuid_get (key2));
    if (ret == 0) {
        ret = NUMCMP (msg_hash_matchtag_get (key1),
                      msg_hash_matchtag_get (key2));
    }
    return ret;
}

/* N.B. zhashx_destructor_fn signature
 */
static void msg_hash_destructor (void **item)
{
    if (item) {
        flux_msg_decref (*item);
        item = NULL;
    }
}

zhashx_t *msg_hash_create (msg_hash_type_t type)
{
    zhashx_t *hash;

    if (!(hash = zhashx_new ())) {
        errno = ENOMEM;
        return NULL;
    }
    switch (type) {
        case MSG_HASH_TYPE_UUID_MATCHTAG:
            zhashx_set_key_hasher (hash, msg_hash_uuid_matchtag_hasher);
            zhashx_set_key_comparator (hash, msg_hash_uuid_matchtag_key_cmp);
            break;
        default:
            zhashx_destroy (&hash);
            errno = EINVAL;
            return NULL;
    }
    zhashx_set_key_duplicator (hash, NULL);
    zhashx_set_key_destructor (hash, NULL);
    zhashx_set_duplicator (hash, (zhashx_duplicator_fn *)flux_msg_incref);
    zhashx_set_destructor (hash, msg_hash_destructor);

    return hash;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
