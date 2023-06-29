/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include <jansson.h>

#include "src/common/libccan/ccan/base64/base64.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"

#include "ioencode.h"


static json_t *data_encode_base64 (const char *stream,
                                   const char *rank,
                                   const char *data,
                                   int len)
{
    ssize_t n;
    json_t *o = NULL;
    char *dest = NULL;
    size_t destlen = base64_encoded_length (len) + 1; /* +1 for NUL */

    if ((dest = malloc (destlen))
        && (n = base64_encode (dest, destlen, data, len)) >= 0) {
        if (!(o = json_pack ("{s:s s:s s:s s:s#}",
                             "stream", stream,
                             "rank", rank,
                             "encoding", "base64",
                             "data", dest, n)))
            errno = ENOMEM;
    }
    ERRNO_SAFE_WRAP (free, dest);
    return o;
}

json_t *ioencode (const char *stream,
                  const char *rank,
                  const char *data,
                  int len,
                  bool eof)
{
    json_t *o = NULL;

    /* data can be NULL and len == 0 if eof true */
    if (!stream
        || !rank
        || (data && len <= 0)
        || (!data && len != 0)
        || (!data && !len && !eof)) {
        errno = EINVAL;
        return NULL;
    }

    if (data && len) {
        if (!(o = json_pack ("{s:s s:s s:s#}",
                             "stream", stream,
                             "rank", rank,
                             "data", data, len))) {
            /*
             *  json_pack() may fail if there are bytes that cannot
             *   be encoded as UTF-8 - try again, this time
             *   encoding the data in base64:
             */
            if (!(o = data_encode_base64 (stream, rank, data, len)))
                return NULL;
        }
    }
    else {
        if (!(o = json_pack ("{s:s s:s}",
                             "stream", stream,
                             "rank", rank))) {
            errno = ENOMEM;
            return NULL;
        }
    }
    if (eof) {
        if (json_object_set_new (o, "eof", json_true ()) < 0) {
            json_decref (o);
            errno = ENOMEM;
            return NULL;
        }
    }
    return o;
}

static int decode_data_base64 (char *src,
                               size_t srclen,
                               char **datap,
                               size_t *lenp)
{
    ssize_t rc;
    size_t size = base64_decoded_length (srclen);
    if (datap) {
        if (!(*datap = malloc (size)))
            return -1;
        if ((rc = base64_decode (*datap, size, src, srclen)) < 0) {
            ERRNO_SAFE_WRAP (free, (*datap));
            return -1;
        }
        if (lenp)
            *lenp = rc;
    }
    else if (lenp)
        *lenp = size;
    return 0;
}


int iodecode (json_t *o,
              const char **streamp,
              const char **rankp,
              char **datap,
              int *lenp,
              bool *eofp)
{
    const char *stream;
    const char *rank;
    const char *encoding = NULL;
    size_t bin_len = 0;
    char *bufp = NULL;
    char *data = NULL;
    size_t len = 0;
    int eof = 0;
    bool has_data = false;
    bool has_eof = false;

    if (!o) {
        errno = EINVAL;
        return -1;
    }

    if (json_unpack (o, "{s:s s:s s?s}",
                     "stream", &stream,
                     "rank", &rank,
                     "encoding", &encoding) < 0) {
        errno = EPROTO;
        return -1;
    }
    if (json_unpack (o, "{s:s%}", "data", &data, &len) == 0)
        has_data = true;
    if (json_unpack (o, "{s:b}", "eof", &eof) == 0)
        has_eof = true;

    if (!has_data && !has_eof) {
        errno = EPROTO;
        return -1;
    }

    if (streamp)
        (*streamp) = stream;
    if (rankp)
        (*rankp) = rank;
    if (datap || lenp) {
        if (data) {
            if (encoding && streq (encoding, "base64")) {
                if (decode_data_base64 (data, len, &bufp, &bin_len) < 0)
                    return -1;
            }
            else {
                bin_len = len;
                if (datap) {
                    if (!(bufp = malloc (bin_len)))
                        return -1;
                    memcpy (bufp, data, bin_len);
                }
            }
        }
    }
    if (datap)
        (*datap) = bufp;
    if (lenp)
        (*lenp) = bin_len;
    if (eofp)
        (*eofp) = eof;
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
