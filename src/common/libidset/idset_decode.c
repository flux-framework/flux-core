/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
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
#include <sys/param.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

#include "idset.h"
#include "idset_private.h"

static int parse_range (const char *s, unsigned int *hi, unsigned int *lo)
{
    char *endptr;
    unsigned int h, l;
    unsigned long n;

    n = strtoul (s, &endptr, 10);
    if (n >= UINT_MAX || endptr == s || (*endptr != '\0' && *endptr != '-'))
        return -1;
    h = l = n;
    if (*endptr == '-') {
        s = endptr + 1;
        n = strtoul (s, &endptr, 10);
        if (n >= UINT_MAX || endptr == s || *endptr != '\0')
            return -1;
        h = n;
    }
    if (h >= l) {
        *hi = h;
        *lo = l;
    }
    else {
        *hi = l;
        *lo = h;
    }
    return 0;
}

static char *trim_brackets (char *s)
{
    char *p = s;
    if (*p == '[')
        p++;
    size_t len = strlen (p);
    if (len > 0 && p[len - 1] == ']')
        p[len - 1] = '\0';
    return p;
}

struct idset *idset_decode (const char *str)
{
    struct idset *idset;
    char *cpy = NULL;
    char *tok, *saveptr, *a1;
    int saved_errno;

    if (!str) {
        errno = EINVAL;
        return NULL;
    }
    if (!(idset = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return NULL;
    if (!(cpy = strdup (str)))
        goto error;
    a1 = trim_brackets (cpy);
    saveptr = NULL;
    while ((tok = strtok_r (a1, ",", &saveptr))) {
        unsigned int hi, lo, i;
        if (parse_range (tok, &hi, &lo) < 0)
            goto inval;
        /* Count backwards so that idset_set() can grow the
         * idset to the maximum size on the first access,
         * rather than possibly doing it multiple times.
         */
        for (i = hi; i >= lo && i != UINT_MAX; i--) {
            if (idset_set (idset, i) < 0)
                goto error;
        }
        a1 = NULL;
    }
    free (cpy);
    return idset;
inval:
    errno = EINVAL;
error:
    saved_errno = errno;
    idset_destroy (idset);
    free (cpy);
    errno = saved_errno;
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
