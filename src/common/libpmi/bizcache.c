/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Small business card cache, by rank.
 * Business cards are fetched one by one from the PMI server.  To avoid
 * fetching the same ones more than once in different parts of a client,
 * implement a simple cache.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"

#include "upmi.h"
#include "bizcard.h"

#include "bizcache.h"

struct bizcache {
    struct upmi *upmi;
    size_t size;
    struct bizcard **cards; // indexed by rank, array storage follows struct
};

struct bizcache *bizcache_create (struct upmi *upmi, size_t size)
{
    struct bizcache *cache;

    cache = calloc (1, sizeof (*cache) + size * sizeof (struct bizcard *));
    if (!cache)
        return NULL;
    cache->upmi = upmi;
    cache->size = size;
    cache->cards = (struct bizcard **)(cache + 1);
    return cache;
}

void bizcache_destroy (struct bizcache *cache)
{
    if (cache) {
        int saved_errno = errno;
        for (int i = 0; i < cache->size; i++)
            bizcard_decref (cache->cards[i]);
        free (cache);
        errno = saved_errno;
    }
}

static struct bizcard *bizcache_lookup (struct bizcache *cache, int rank)
{
    if (rank < 0 || rank >= cache->size)
        return NULL;
    return cache->cards[rank];
}

static int bizcache_insert_new (struct bizcache *cache,
                                int rank,
                                struct bizcard *bc)
{
    if (rank < 0 || rank >= cache->size) {
        errno = EINVAL;
        return -1;
    }
    bizcard_decref (cache->cards[rank]);
    cache->cards[rank] = bc;
    return 0;
}

/* Put business card directly to PMI using rank as key.
 */
int bizcache_put (struct bizcache *cache,
                  int rank,
                  const struct bizcard *bc,
                  flux_error_t *error)
{
    char key[64];
    const char *s;
    flux_error_t e;

    (void)snprintf (key, sizeof (key), "%d", rank);
    if (!(s = bizcard_encode (bc))) {
        errprintf (error, "error encoding business card: %s", strerror (errno));
        return -1;
    }
    if (upmi_put (cache->upmi, key, s, &e) < 0) {
        errprintf (error,
                   "%s: put %s: %s",
                   upmi_describe (cache->upmi),
                   key,
                   e.text);
        return -1;
    }
    return 0;
}

/* Return business card from cache, filling the cache entry by fetching
 * it from PMI if missing.  The caller must not free the business card.
 */
int bizcache_get (struct bizcache *cache,
                  int rank,
                  const struct bizcard **bcp,
                  flux_error_t *error)
{
    char key[64];
    char *val;
    flux_error_t e;
    struct bizcard *bc;

    if ((bc = bizcache_lookup (cache, rank))) {
        *bcp = bc;
        return 0;
    }

    (void)snprintf (key, sizeof (key), "%d", rank);
    if (upmi_get (cache->upmi, key, rank, &val, &e) < 0) {
        errprintf (error,
                   "%s: get %s: %s",
                   upmi_describe (cache->upmi),
                   key,
                   e.text);
        return -1;
    }
    if (!(bc = bizcard_decode (val, &e))) {
        errprintf (error,
                   "error decoding rank %d business card: %s",
                   rank,
                   e.text);
        goto error;
    }
    if (bizcache_insert_new (cache, rank, bc) < 0) {
        errprintf (error, "error caching rank %d business card", rank);
        bizcard_decref (bc);
        goto error;
    }
    free (val);
    *bcp = bc;
    return 0;
error:
    ERRNO_SAFE_WRAP (free, val);
    return -1;
}

// vi:ts=4 sw=4 expandtab
