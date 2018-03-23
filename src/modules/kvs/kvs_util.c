/*****************************************************************************\
 *  Copyright (c) 2015 Lawrence Livermore National Security, LLC.  Produced at
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
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

char *kvs_util_normalize_key (const char *key, bool *want_directory)
{
    const char sep = '.';
    char *cpy = strdup (key);
    int i, len = strlen (key) + 1;
    bool has_sep_suffix = false;

    if (cpy) {
        /* Transform duplicate path separators into a single one.
         */
        for (i = 0; i < len; ) {
            if (cpy[i] == sep && cpy[i + 1] == sep) {
                memmove (&cpy[i], &cpy[i + 1], len - i - 1);
                len--;
            }
            else
                i++;
        }
        /* Eliminate leading path separator
         */
        if (len > 2 && cpy[0] == sep) {
            memmove (&cpy[0], &cpy[1], len - 1);
            len--;
        }
        /* Eliminate trailing path separator
         */
        if (len > 2 && cpy[len - 2] == sep) {
            cpy[len - 2] = '\0';
            len--;
            has_sep_suffix = true;
        }
        if (cpy[0] == sep)
            has_sep_suffix = true;
        if (want_directory)
            *want_directory = has_sep_suffix;
    }
    return cpy;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
