/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_ERRPRINTF_H
#define _UTIL_ERRPRINTF_H

#include <stdarg.h>
#include <string.h>

#include "src/common/libflux/types.h" /* flux_error_t */

/*
 *  Utility function for printing an error to a flux_error_t container.
 *  This function always returns -1 as a convenience in error handling
 *   functions, e.g.
 *
 *  return errprintf (errp, "Function failed");
 *
 */
int errprintf (flux_error_t *errp, const char *fmt, ...)
     __attribute__ ((format (printf, 2, 3)));

int verrprintf (flux_error_t *errp, const char *fmt, va_list ap);


static inline void err_init (flux_error_t *errp)
{
    if (errp)
        memset (errp->text, 0, sizeof (errp->text));
}

#endif /* !_UTIL_ERRPRINTF_H */

// vi:ts=4 sw=4 expandtab
