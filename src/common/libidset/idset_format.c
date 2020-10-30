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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
