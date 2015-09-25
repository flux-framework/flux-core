/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/param.h>

#include "intarray.h"

static int count_delimiters (const char *s, char delimiter)
{
    char *p = (char *)s;
    int count = 0;

    while ((p = strchr (p, delimiter))) {
        count++;
        p++;
    }
    return count;
}

static int nextint (char **s, char delimiter, int *val)
{
    char *endptr;
    int i;

    i = strtol (*s, &endptr, 0);
    if (i > INT_MAX || i < INT_MIN) {
        errno = ERANGE;
        return -1;
    }
    if (endptr == *s) {
        errno = EINVAL;
        return -1;
    }
    if (*endptr == delimiter)
        *s = endptr + 1;
    else if (*endptr == '\0')
        *s = endptr;
    else {
        errno = EINVAL;
        return -1;
    }
    *val = i;
    return 0;
}

int intarray_create (const char *s, int **ia, int *ia_count)
{
    char *p = (char *)s;
    int i, count, *a;

    count = count_delimiters (s, ',') + 1;
    if (!(a = malloc (sizeof (a[0]) * count))) {
        errno = ENOMEM;
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (nextint (&p, ',', &a[i]) < 0) {
            free (a);
            return -1;
        }
    }
    *ia = a;
    *ia_count = count;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
