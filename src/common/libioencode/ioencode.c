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

#include "src/common/libutil/macros.h"

#include "ioencode.h"

json_t *ioencode (const char *stream,
                  const char *rank,
                  const char *data,
                  int len,
                  bool eof)
{
    json_t *o = NULL;
    json_t *rv = NULL;

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
                             "data", data, len)))
            goto error;
    }
    else {
        if (!(o = json_pack ("{s:s s:s}",
                             "stream", stream,
                             "rank", rank)))
            goto error;
    }
    if (eof) {
        if (json_object_set_new (o, "eof", json_true ()) < 0)
            goto error;
    }
    rv = o;
error:
    return rv;
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
                     "rank", &rank) < 0)
        goto cleanup;
    if (json_unpack (o, "{s:s%}", "data", &bin_data, &bin_len) == 0) {
        has_data = true;
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
        *datap = NULL;
        if (bin_data) {
            if (!(*datap = malloc (bin_len)))
                goto cleanup;
            memcpy (*datap, bin_data, bin_len);
            bin_data = NULL;
        }
    }
    if (lenp)
        (*lenp) = bin_len;
    if (eofp)
        (*eofp) = eof;

    rv = 0;
cleanup:
    return rv;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
