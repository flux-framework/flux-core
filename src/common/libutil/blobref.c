/*****************************************************************************\
 *  Copyright (c) 2015 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <ctype.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>

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

static void sha1_hash (const void *data, int data_len, void *hash, int hash_len);
static void sha256_hash (const void *data, int data_len, void *hash, int hash_len);

struct blobhash {
    char *name;
    int hashlen;
    void (*hashfun)(const void *data, int data_len, void *hash, int hash_len);
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

static void sha1_hash (const void *data, int data_len, void *hash, int hash_len)
{
    SHA1_CTX ctx;

    assert (hash_len == SHA1_DIGEST_SIZE);
    SHA1_Init (&ctx);
    SHA1_Update (&ctx, data, data_len);
    SHA1_Final (&ctx, hash);
}

static void sha256_hash (const void *data, int data_len, void *hash, int hash_len)
{
    SHA256_CTX ctx;

    assert (hash_len == SHA256_BLOCK_SIZE);
    sha256_init (&ctx);
    sha256_update (&ctx, data, data_len);
    sha256_final (&ctx, hash);
}

/* true if s1 contains "s2-" prefix
 */
static int prefixmatch (const char *s1, const char *s2)
{
    int len = strlen (s2);
    if (strlen (s1) < len + 1 || s1[len] != '-')
        return 0;
    return !strncmp (s1, s2, len);
}

static struct blobhash *lookup_blobhash (const char *name)
{
    struct blobhash *bh;

    for (bh = &blobtab[0]; bh->name != NULL; bh++)
        if (!strcmp (name, bh->name) || prefixmatch (name, bh->name))
            return bh;
    return NULL;
}

static uint8_t xtoint (char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 0xA;
    /* (c >= 'a' && c <= 'f') */
    return c - 'a' + 0xA;
}

static char inttox (uint8_t i)
{
    if (i <= 9)
        return '0' + i;
    return 'a' + i - 0xa;
}

static bool isxdigit_lower (char c)
{
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
        return true;
    return false;
}

int blobref_strtohash (const char *blobref, void *hash, int size)
{
    struct blobhash *bh;
    uint8_t *ihash = (uint8_t *)hash;
    int i;

    if (!(bh = lookup_blobhash (blobref)) || size < bh->hashlen)
        goto inval;
    if (strlen (blobref) != bh->hashlen*2 + strlen (bh->name) + 1)
        goto inval;
    blobref += strlen (bh->name) + 1;
    for (i = 0; i < bh->hashlen; i++) {
        if (!isxdigit_lower (blobref[i*2]))
            goto inval;
        ihash[i] = xtoint (blobref[i*2]) << 4;
        if (!isxdigit_lower (blobref[i*2 + 1]))
            goto inval;
        ihash[i] |= xtoint (blobref[i*2 + 1]);
    }
    return bh->hashlen;
inval:
    errno = EINVAL;
    return -1;
}

static int hashtostr (struct blobhash *bh,
                      const void *hash, int len,
                      char *blobref, int blobref_len)
{
    uint8_t *ihash = (uint8_t *)hash;
    int i;

    if (len != bh->hashlen
        || !blobref
        || blobref_len < bh->hashlen*2 + strlen (bh->name) + 2) {
        errno = EINVAL;
        return -1;
    }
    strcpy (blobref, bh->name);
    strcat (blobref, "-");
    blobref += strlen (bh->name) + 1;
    for (i = 0; i < bh->hashlen; i++) {
        blobref[i*2] = inttox (ihash[i] >> 4);
        blobref[i*2 + 1] = inttox (ihash[i] & 0xf);
    }
    blobref[i*2] = '\0';
    return 0;
}

int blobref_hashtostr (const char *hashtype,
                       const void *hash, int len,
                       void *blobref, int blobref_len)
{
    struct blobhash *bh;

    if (!(bh = lookup_blobhash (hashtype))) {
        errno = EINVAL;
        return -1;
    }
    return hashtostr (bh, hash, len, blobref, blobref_len);
}


int blobref_hash (const char *hashtype,
                  const void *data, int len,
                  void *blobref, int blobref_len)
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

int blobref_validate (const char *blobref)
{
    struct blobhash *bh;

    if (!blobref || !(bh = lookup_blobhash (blobref))
                 || strlen (blobref) != bh->hashlen*2 + strlen (bh->name) + 1) {
        errno = EINVAL;
        return -1;
    }
    blobref += strlen (bh->name) + 1;
    while (*blobref) {
        if (!isxdigit_lower (*blobref++)) {
            errno = EINVAL;
            return -1;
        }
    }
    return 0;
}

int blobref_validate_hashtype (const char *name)
{
    if (name == NULL || !lookup_blobhash (name))
        return -1;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
