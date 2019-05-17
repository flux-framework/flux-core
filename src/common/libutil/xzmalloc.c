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
#    include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xzmalloc.h"
#include "oom.h"

void *xzmalloc (size_t size)
{
    void *new;

    new = malloc (size);
    if (!new)
        oom ();
    memset (new, 0, size);
    return new;
}

void *xrealloc (void *ptr, size_t size)
{
    void *new = realloc (ptr, size);
    if (!new)
        oom ();
    return new;
}

char *xstrdup (const char *s)
{
    char *cpy = strdup (s);
    if (!cpy)
        oom ();
    return cpy;
}

char *xvasprintf (const char *fmt, va_list ap)
{
    char *s;

    if (vasprintf (&s, fmt, ap) < 0)
        oom ();
    return s;
}

char *xasprintf (const char *fmt, ...)
{
    va_list ap;
    char *s;

    va_start (ap, fmt);
    s = xvasprintf (fmt, ap);
    va_end (ap);
    return s;
}

char *xstrsub (const char *str, char a, char b)
{
    char *cpy = xstrdup (str);
    char *s = cpy;
    while (*s) {
        if (*s == a)
            *s = b;
        s++;
    }
    return cpy;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
