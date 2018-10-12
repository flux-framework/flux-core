/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <argz.h>

#include "src/common/libutil/base64.h"

#include "sign_none.h"

int header_decode (const char *src, int srclen, uint32_t *useridp)
{
    int dstlen;
    char *dst = NULL;
    char *entry = NULL;
    char *key;
    char *val;
    char *val_userid = NULL;
    char *val_version = NULL;
    char *val_mech = NULL;
    uint32_t userid;
    char *endptr;

    dstlen = base64_decode_length (srclen);
    if (!(dst = calloc (1, dstlen)))
        return -1;
    if (base64_decode_block (dst, &dstlen, src, srclen) < 0)
        goto error_inval;
    if (dst[dstlen - 1] != '\0')
        goto error_inval;
    while ((key = entry = argz_next (dst, dstlen, entry))) {
        if (!(val = entry = argz_next (dst, dstlen, entry)))
            goto error_inval;
        if (!strcmp (key, "version"))
            val_version = val;
        else if (!strcmp (key, "userid"))
            val_userid = val;
        else if (!strcmp (key, "mech"))
            val_mech = val;
        else
            goto error_inval;
    }
    if (!val_version || !val_mech || strcmp (val_version, "i1") != 0
                                  || strcmp (val_mech, "snone") != 0)
        goto error_inval;
    if (!val_userid || *val_userid != 'i')
        goto error_inval;
    if (val_userid[1] < '0' || val_userid[1] > '9') // no sign or wspace allowed
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
    int dstlen;
    int i;

    srclen = snprintf (src, sizeof (src), "version:i1:userid:i%lu:mech:snone:",
                       (unsigned long)userid);
    assert (srclen < sizeof (src));
    for (i = 0; i < srclen; i++) {
        if (src[i] == ':')
            src[i] = '\0';
    }
    dstlen = base64_encode_length (srclen);
    if (!(dst = calloc (1, dstlen)))
        return NULL;
    (void)base64_encode_block (dst, &dstlen, src, srclen);
    return dst;
}

static char *payload_encode (const void *src, int srclen)
{
    char *dst;
    int dstlen;

    dstlen = base64_encode_length (srclen);
    if (!(dst = calloc (1, dstlen)))
        return NULL;
    (void)base64_encode_block (dst, &dstlen, src, srclen);
    return dst;
}

static int payload_decode (const void *src, int srclen,
                           void **payload, int *payloadsz)
{
    int dstlen;
    void *dst;

    dstlen = base64_decode_length (srclen);
    if (!(dst = calloc (1, dstlen)))
        return -1;
    if (base64_decode_block (dst, &dstlen, src, srclen) < 0)
        goto error_inval;
    *payload = dst;
    *payloadsz = dstlen;
    return 0;
error_inval:
    free (dst);
    errno = EINVAL;
    return -1;
}

char *sign_none_wrap (const void *payload, int payloadsz,
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
                      void **payload, int *payloadsz,
                      uint32_t *userid)
{
    char *p;

    if (!input || !userid || !payload || !payloadsz)
        goto error_inval;
    if (!(p = strchr (input, '.')))
        goto error_inval;
    if (header_decode (input, p - input, userid) < 0)
        goto error;
    input = p + 1;
    if (!(p = strchr (input, '.')))
        goto error_inval;
    if (strcmp (p, ".none") != 0)
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
