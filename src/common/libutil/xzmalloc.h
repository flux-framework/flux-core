/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_XZMALLOC_H
#    define _UTIL_XZMALLOC_H

#    include <sys/types.h>
#    include <stdarg.h>

/* Memory allocation functions that call oom() on allocation error.
 */
void *xzmalloc (size_t size);
void *xrealloc (void *ptr, size_t size);
char *xstrdup (const char *s);
char *xvasprintf (const char *fmt, va_list ap);
char *xasprintf (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
char *xstrsub (const char *str, char a, char b);

#endif /* !_UTIL_XZMALLOC_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
