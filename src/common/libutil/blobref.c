/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
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
#include <ctype.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>

#include "ccan/str/str.h"
#include "ccan/str/hex/hex.h"

#include "blobref.h"
#include "sha1.h"
#include "sha256.h"

#define SHA1_PREFIX_STRING  "sha1-"
#define SHA1_PREFIX_LENGTH  5
#define SHA1_STRING_SIZE    (SHA1_DIGEST_SIZE*2 + SHA1_PREFIX_LENGTH + 1)

#define SHA256_PREFIX_STRING  "sha256-"
#define SHA256_PREFIX_LENGTH  7
#define SHA256_STRING_SIZE    (SHA256_BLOCK_SIZE*2 + SHA256_PREFIX_LENGTH + 1)

#if BLOBREF_MAX_STRING_SIZE < SHA1_STRING_SIZE
#error BLOBREF_MAX_STRING_SIZE is too small
#endif
#if BLOBREF_MAX_DIGEST_SIZE < SHA1_DIGEST_SIZE
#error BLOBREF_MAX_DIGEST_SIZE is too small
#endif
#if BLOBREF_MAX_STRING_SIZE < SHA256_STRING_SIZE
#error BLOBREF_MAX_STRING_SIZE is too small
#endif
#if BLOBREF_MAX_DIGEST_SIZE < SHA256_BLOCK_SIZE
#error BLOBREF_MAX_DIGEST_SIZE is too small
#endif

static void sha1_hash (const void *data,
                       size_t data_len,
                       void *hash,
                       size_t hash_len);
static void sha256_hash (const void *data,
                         size_t data_len,
                         void *hash,
                         size_t hash_len);

struct blobhash {
    char *name;
    size_t hashlen;
    void (*hashfun)(const void *data,
                    size_t data_len,
                    void *hash,
                    size_t hash_len);
};

static struct blobhash blobtab[] = {
    { .name = "sha1",
      .hashlen = SHA1_DIGEST_SIZE,
      .hashfun = sha1_hash,
    },
    { .name = "sha256",
      .hashlen = SHA256_BLOCK_SIZE,
      .hashfun = sha256_hash,
    },
    { NULL, 0, 0 },
};

static void sha1_hash (const void *data,
                       size_t data_len,
                       void *hash,
                       size_t hash_len)
{
    SHA1_CTX ctx;

    assert (hash_len == SHA1_DIGEST_SIZE);
    SHA1_Init (&ctx);
    SHA1_Update (&ctx, data, data_len);
    SHA1_Final (&ctx, hash);
}

static void sha256_hash (const void *data,
                         size_t data_len,
                         void *hash,
                         size_t hash_len)
{
    SHA256_CTX ctx;

    assert (hash_len == SHA256_BLOCK_SIZE);
    sha256_init (&ctx);
    sha256_update (&ctx, data, data_len);
    sha256_final (&ctx, hash);
}

/* true if s1 contains "s2-" prefix
 */
static bool prefixmatch (const char *s1, const char *s2)
{
    if (!strstarts (s1, s2))
        return false;
    s1 += strlen (s2);
    if (*s1 != '-')
        return false;
    return true;
}

static struct blobhash *lookup_blobhash (const char *name)
{
    struct blobhash *bh;

    for (bh = &blobtab[0]; bh->name != NULL; bh++)
        if (streq (name, bh->name) || prefixmatch (name, bh->name))
            return bh;
    return NULL;
}

static bool isxdigit_lower (char c)
{
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
        return true;
    return false;
}

ssize_t blobref_strtohash (const char *blobref, void *hash, size_t size)
{
    struct blobhash *bh;
    size_t len = strlen (blobref);
    size_t offset;

    if (!(bh = lookup_blobhash (blobref)) || size < bh->hashlen)
        goto inval;
    offset = strlen (bh->name) + 1;
    if (len - offset + 1 != hex_str_size (bh->hashlen))
        goto inval;
    if (!hex_decode (blobref + offset, len - offset, hash, bh->hashlen))
        goto inval;
    return bh->hashlen;
inval:
    errno = EINVAL;
    return -1;
}

static int hashtostr (struct blobhash *bh,
                      const void *hash,
                      size_t len,
                      char *blobref,
                      size_t blobref_len)
{
    size_t offset;

    if (len != bh->hashlen || !blobref)
        goto inval;
    offset = strlen (bh->name) + 1;
    if (blobref_len < hex_str_size (bh->hashlen) + offset - 1)
        goto inval;
    strcpy (blobref, bh->name);
    strcat (blobref, "-");
    if (!hex_encode (hash, len, blobref + offset, blobref_len - offset))
        goto inval;
    return 0;
inval:
    errno = EINVAL;
    return -1;
}

int blobref_hashtostr (const char *hashtype,
                       const void *hash,
                       size_t len,
                       void *blobref,
                       size_t blobref_len)
{
    struct blobhash *bh;

    if (!(bh = lookup_blobhash (hashtype))) {
        errno = EINVAL;
        return -1;
    }
    return hashtostr (bh, hash, len, blobref, blobref_len);
}


int blobref_hash (const char *hashtype,
                  const void *data,
                  size_t len,
                  void *blobref,
                  size_t blobref_len)
{
    struct blobhash *bh;
    uint8_t hash[BLOBREF_MAX_DIGEST_SIZE];

    if (!(bh = lookup_blobhash (hashtype))) {
        errno = EINVAL;
        return -1;
    }
    bh->hashfun (data, len, hash, bh->hashlen);
    return hashtostr (bh, hash, bh->hashlen, blobref, blobref_len);
}

ssize_t blobref_hash_raw (const char *hashtype,
                          const void *data,
                          size_t len,
                          void *hash,
                          size_t hash_len)
{
    struct blobhash *bh;

    if (!hashtype
        || !(bh = lookup_blobhash (hashtype))
        || hash_len < bh->hashlen
        || !hash) {
        errno = EINVAL;
        return -1;
    }
    bh->hashfun (data, len, hash, bh->hashlen);
    return bh->hashlen;
}

int blobref_validate (const char *blobref)
{
    struct blobhash *bh;
    size_t len;
    size_t offset;

    if (!blobref || !(bh = lookup_blobhash (blobref)))
        goto inval;
    len = strlen (blobref);
    offset = strlen (bh->name) + 1;
    if (len - offset + 1 != hex_str_size (bh->hashlen))
        goto inval;
    blobref += offset;
    while (*blobref) {
        if (!isxdigit_lower (*blobref++))
            goto inval;
    }
    return 0;
inval:
    errno = EINVAL;
    return -1;
}

ssize_t blobref_validate_hashtype (const char *name)
{
    struct blobhash *bh;

    if (name == NULL || !(bh = lookup_blobhash (name))) {
        errno = EINVAL;
        return -1;
    }
    return bh->hashlen;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
