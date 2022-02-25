/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
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
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include "errprintf.h"

int errprintf (flux_error_t *errp, const char *fmt, ...)
{
    if (errp) {
        int saved_errno = errno;
        memset (errp->text, 0, sizeof (errp->text));
        if (fmt) {
            va_list ap;
            int n;
            va_start (ap, fmt);
            n = vsnprintf (errp->text, sizeof (errp->text), fmt, ap);
            if (n > sizeof (errp->text))
                errp->text[sizeof (errp->text) - 2] = '+';
            va_end (ap);
        }
        errno = saved_errno;
    }
    return -1;
}

// vi:ts=4 sw=4 expandtab
