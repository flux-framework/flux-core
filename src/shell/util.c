/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* job shell info */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>
#include <string.h>

#include "src/common/libutil/log.h"

#include "util.h"

int shell_util_taskid_path (const char *path,
                            int rank,
                            char *pathbuf,
                            int pathbuflen)
{
    char *ptr;
    char buf[32];
    int len, buflen, total_len;

    if (!(ptr = strstr (path, "{{taskid}}"))) {
        errno = EINVAL;
        return -1;
    }

    len = strlen (path);
    buflen = snprintf (buf, sizeof (buf), "%d", rank);
    /* -10 for {{taskid}}, +1 for NUL */
    total_len = len - 10 + buflen + 1;
    if (total_len > pathbuflen) {
        errno = EOVERFLOW;
        return -1;
    }
    memset (pathbuf, '\0', pathbuflen);
    memcpy (pathbuf, path, ptr - path);
    memcpy (pathbuf + (ptr - path), buf, buflen);
    memcpy (pathbuf + (ptr - path) + buflen, ptr + 10, len - (ptr - path) - 10);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
