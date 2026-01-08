/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif

#include "ccan/base64/base64.h"
#include "ccan/str/str.h"

#include "sign_none.h"

int header_decode (const char *src, int srclen, uint32_t *useridp)
{
    size_t dstbuflen = base64_decoded_length (srclen) + 1; /* +1 for NUL */
    ssize_t dstlen;
    char *dst;
    char *entry = NULL;
    char *key;
    char *val;
    char *val_userid = NULL;
    char *val_version = NULL;
    char *val_mech = NULL;
    uint32_t userid;
    char *endptr;

    if (!(dst = malloc (dstbuflen)))
        return -1;
    if ((dstlen = base64_decode (dst, dstbuflen, src, srclen)) < 0)
        goto error_inval;
    while ((key = entry = argz_next (dst, dstlen, entry))) {
        if (!(val = entry = argz_next (dst, dstlen, entry)))
            goto error_inval;
        if (streq (key, "version"))
            val_version = val;
        else if (streq (key, "userid"))
            val_userid = val;
        else if (streq (key, "mechanism"))
            val_mech = val;
        else
            goto error_inval;
    }
    if (!val_version
        || !val_mech
        || !streq (val_version, "i1")
        || !streq (val_mech, "snone")
        || !val_userid
        || *val_userid != 'i'
        || val_userid[1] < '0'
        || val_userid[1] > '9') // no sign or wspace allowed
        goto error_inval;
    errno = 0;
    userid = strtoul (val_userid + 1, &endptr, 10);
    if (errno != 0 || *endptr != '\0' || endptr == val_userid)
        goto error_inval;
    free (dst);
    *useridp = userid;
    return 0;
error_inval:
    free (dst);
    errno = EINVAL;
    return -1;
}

static char *header_encode (uint32_t userid)
{
    char src[128];
    int srclen;
    char *dst;
    size_t dstbuflen;
    int i;

    srclen = snprintf (src,
                       sizeof (src),
                       "version:i1:userid:i%lu:mechanism:snone:",
                       (unsigned long)userid);
    assert (srclen < sizeof (src));
    for (i = 0; i < srclen; i++) {
        if (src[i] == ':')
            src[i] = '\0';
    }
    dstbuflen = base64_encoded_length (srclen) + 1; /* +1 for NUL */
    if (!(dst = malloc (dstbuflen)))
        return NULL;
    if (base64_encode (dst, dstbuflen, src, srclen) < 0)
        return NULL;
    return dst;
}

static char *payload_encode (const char *src, int srclen)
{
    char *dst;
    size_t dstbuflen;

    dstbuflen = base64_encoded_length (srclen) + 1; /* +1 for NUL */
    if (!(dst = malloc (dstbuflen)))
        return NULL;
    if (base64_encode (dst, dstbuflen, src, srclen) < 0) {
        free (dst);
        return NULL;
    }
    return dst;
}

static int payload_decode (const void *src,
                           int srclen,
                           void **payload,
                           int *payloadsz)
{
    size_t dstbuflen = base64_decoded_length (srclen) + 1; /* +1 for NUL */
    ssize_t dstlen;
    char *dst;

    if (!(dst = malloc (dstbuflen)))
        return -1;
    if ((dstlen = base64_decode (dst, dstbuflen, src, srclen)) < 0)
        goto error_inval;
    *payload = dst;
    *payloadsz = dstlen;
    return 0;
error_inval:
    free (dst);
    errno = EINVAL;
    return -1;
}

char *sign_none_wrap (const void *payload,
                      int payloadsz,
                      uint32_t userid)
{
    char *h = NULL;
    char *p = NULL;
    char *result;

    if (payloadsz < 0 || (payloadsz > 0 && payload == NULL)) {
        errno = EINVAL;
        return NULL;
    }
    if (!(h = header_encode (userid)))
        goto error_nomem;
    if (!(p = payload_encode (payload, payloadsz)))
        goto error_nomem;
    if (asprintf (&result, "%s.%s.none", h, p) < 0)
        goto error_nomem;
    free (h);
    free (p);
    return result;
error_nomem:
    free (h);
    free (p);
    errno = ENOMEM;
    return NULL;
}

int sign_none_unwrap (const char *input,
                      void **payload,
                      int *payloadsz,
                      uint32_t *userid)
{
    const char *p;

    if (!input || !userid || !payload || !payloadsz)
        goto error_inval;
    if (!(p = strchr (input, '.')))
        goto error_inval;
    if (header_decode (input, p - input, userid) < 0)
        goto error;
    input = p + 1;
    if (!(p = strchr (input, '.')))
        goto error_inval;
    if (!streq (p, ".none"))
        goto error_inval;
    if (payload_decode (input, p - input, payload, payloadsz) < 0)
        goto error;
    return 0;
error_inval:
    errno = EINVAL;
error:
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
