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
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/log.h"

#include "types.h"

char *kvs_util_json_dumps (json_t *o)
{
    /* Must pass JSON_ENCODE_ANY, can be called on any object.  Must
     * set JSON_SORT_KEYS, two different objects with different
     * internal order should map to same string (and reference when
     * used by json_hash()).
     */
    int flags = JSON_ENCODE_ANY | JSON_COMPACT | JSON_SORT_KEYS;
    char *s;
    if (!o) {
        if (!(s = strdup ("null"))) {
            errno = ENOMEM;
            return NULL;
        }
        return s;
    }
    if (!(s = json_dumps (o, flags))) {
        errno = ENOMEM;
        return NULL;
    }
    return s;
}

int kvs_util_json_encoded_size (json_t *o, size_t *size)
{
    char *s = kvs_util_json_dumps (o);
    if (!s) {
        errno = ENOMEM;
        return -1;
    }
    if (size)
        *size = strlen (s);
    free (s);
    return 0;
}

int kvs_util_json_hash (const char *hash_name, json_t *o, href_t ref)
{
    char *s;
    int rc;

    if (!(s = kvs_util_json_dumps (o)))
        return -1;
    rc = blobref_hash (hash_name, (uint8_t *)s, strlen (s) + 1,
                       ref, sizeof (href_t));
    free (s);
    return rc;
}
