/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <sys/param.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

#include "src/common/libutil/errno_safe.h"

#include "idset.h"
#include "idset_private.h"

static int find_brackets (const char *s, const char **start, const char **end)
{
    if (!(*start = strchr (s, '[')))
        return -1;
    if (!(*end = strchr (*start + 1, ']')))
        return -1;
    return 0;
}

int format_first (char *buf,
                  size_t bufsz,
                  const char *fmt,
                  unsigned int id)
{
    const char *start, *end;

    if (!buf || !fmt || find_brackets (fmt, &start, &end) < 0) {
        errno = EINVAL;
        return -1;
    }
    if (snprintf (buf,
                  bufsz,
                  "%.*s%u%s",
                  (int)(start - fmt),
                  fmt,
                  id,
                  end + 1) >= bufsz) {
        errno = EOVERFLOW;
        return -1;
    }
    return 0;
}

static int idset_format_map_ex (const char *s,
                                size_t maxsize,
                                idset_format_map_f fun,
                                void *arg,
                                bool *stop)

{
    const char *start, *end;
    struct idset *idset = NULL;
    char *buf = NULL;
    int count = 0;
    unsigned int id;
    int n;

    if (find_brackets (s, &start, &end) == 0) {
        if (!(idset = idset_ndecode (start, end - start + 1)))
            goto error;
        if (!(buf = malloc (maxsize)))
            goto error;
        id = idset_first (idset);
        while (id != IDSET_INVALID_ID) {
            if (format_first (buf, maxsize, s, id) < 0)
                goto error;
            if ((n = idset_format_map_ex (buf, maxsize, fun, arg, stop)) < 0)
                goto error;
            count += n;
            if (*stop)
                break;
            id = idset_next (idset, id);
        }
        free (buf);
        idset_destroy (idset);
    }
    else {
        if (fun && fun (s, stop, arg) < 0)
            goto error;
        count = 1;
    }
    return count;
error:
    ERRNO_SAFE_WRAP (free, buf);
    idset_destroy (idset);
    return -1;
}

int idset_format_map (const char *s, idset_format_map_f fun, void *arg)
{
    bool stop = false;
    if (!s) {
        errno = EINVAL;
        return -1;
    }
    return idset_format_map_ex (s, 4096, fun, arg, &stop);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
