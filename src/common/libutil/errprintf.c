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

int verrprintf (flux_error_t *errp, const char *fmt, va_list ap)
{
    if (errp) {
        int saved_errno = errno;
        memset (errp->text, 0, sizeof (errp->text));
        if (fmt) {
            int n;
            n = vsnprintf (errp->text, sizeof (errp->text), fmt, ap);
            if (n > sizeof (errp->text))
                errp->text[sizeof (errp->text) - 2] = '+';
        }
        errno = saved_errno;
    }
    return -1;
}

int errprintf (flux_error_t *errp, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    verrprintf (errp, fmt, ap);
    va_end (ap);
    return -1;
}

// vi:ts=4 sw=4 expandtab
