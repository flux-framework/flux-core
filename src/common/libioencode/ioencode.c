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

#include <sodium.h>
#include <jansson.h>

#include "src/common/libutil/macros.h"

#include "ioencode.h"

static char *bin2base64 (const char *bin_data, int bin_len)
{
    char *base64_data;
    int base64_len;

    base64_len = sodium_base64_encoded_len (bin_len, sodium_base64_VARIANT_ORIGINAL);

    if (!(base64_data = calloc (1, base64_len)))
        return NULL;

    sodium_bin2base64 (base64_data, base64_len,
                       (unsigned char *)bin_data, bin_len,
                       sodium_base64_VARIANT_ORIGINAL);

    return base64_data;
}

static char *base642bin (const char *base64_data, size_t *bin_len)
{
    size_t base64_len;
    char *bin_data = NULL;

    base64_len = strlen (base64_data);
    (*bin_len) = BASE64_DECODE_SIZE (base64_len);

    if (!(bin_data = calloc (1, (*bin_len))))
        return NULL;

    if (sodium_base642bin ((unsigned char *)bin_data, (*bin_len),
                           base64_data, base64_len,
                           NULL, bin_len, NULL,
                           sodium_base64_VARIANT_ORIGINAL) < 0) {
        free (bin_data);
        return NULL;
    }

    return bin_data;
}

json_t *ioencode (const char *stream,
                  int rank,
                  const char *data,
                  int len,
                  bool eof)
{
    char *base64_data = NULL;
    json_t *o = NULL;
    json_t *rv = NULL;
    char rankstr[64];

    /* data can be NULL and len == 0 if eof true */
    if (!stream
        || (data && len <= 0)
        || (!data && len != 0)
        || (!data && !len && !eof)) {
        errno = EINVAL;
        return NULL;
    }
    (void)snprintf (rankstr, sizeof (rankstr), "%d", rank);
    if (data && len) {
        if (!(base64_data = bin2base64 (data, len)))
            goto error;
        if (!(o = json_pack ("{s:s s:s s:s}",
                             "stream", stream,
                             "rank", rankstr,
                             "data", base64_data)))
            goto error;
    }
    else {
        if (!(o = json_pack ("{s:s s:s}",
                             "stream", stream,
                             "rank", rankstr)))
            goto error;
    }
    if (eof) {
        if (json_object_set_new (o, "eof", json_true ()) < 0)
            goto error;
    }
    rv = o;
error:
    free (base64_data);
    return rv;
}

int iodecode (json_t *o,
              const char **streamp,
              int *rankp,
              char **datap,
              int *lenp,
              bool *eofp)
{
    const char *stream;
    const char *rankstr;
    int rank;
    const char *base64_data;
    char *bin_data = NULL;
    size_t bin_len = 0;
    int eof = 0;
    bool has_data = false;
    bool has_eof = false;
    int rv = -1;

    if (!o) {
        errno = EINVAL;
        return -1;
    }

    if (json_unpack (o, "{s:s s:s}",
                     "stream", &stream,
                     "rank", &rankstr) < 0)
        goto cleanup;
    rank = strtoul (rankstr, NULL, 10);
    if (json_unpack (o, "{s:s}", "data", &base64_data) == 0) {
        has_data = true;
        if (!(bin_data = base642bin (base64_data, &bin_len)))
            goto cleanup;
    }
    if (json_unpack (o, "{s:b}", "eof", &eof) == 0)
        has_eof = true;

    if (!has_data && !has_eof) {
        errno = EPROTO;
        goto cleanup;
    }

    if (streamp)
        (*streamp) = stream;
    if (rankp)
        (*rankp) = rank;
    if (datap) {
        (*datap) = bin_data;
        bin_data = NULL;
    }
    if (lenp)
        (*lenp) = bin_len;
    if (eofp)
        (*eofp) = eof;

    rv = 0;
cleanup:
    free (bin_data);
    return rv;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
