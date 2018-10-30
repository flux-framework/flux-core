/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
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


char *xvasprintf(const char *fmt, va_list ap)
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
