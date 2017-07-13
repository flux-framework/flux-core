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
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"

#include "types.h"

json_t *json_object_copydir (json_t *dir)
{
    json_t *cpy;
    const char *key;
    json_t *value;

    if (!(cpy = json_object ()))
        oom ();

    json_object_foreach (dir, key, value) {
        if (json_object_set (cpy, key, value) < 0)
            oom ();
    }
    return cpy;
}

bool json_compare (json_t *o1, json_t *o2)
{
    return json_equal (o1, o2);
}

int json_hash (const char *hash_name, json_t *o, href_t ref)
{
    /* Must pass JSON_ENCODE_ANY, json_hash could be done on any object.
     * Must set JSON_SORT_KEYS, two different objects with different internal
     * order should map to same reference */
    char *s = json_dumps (o, JSON_ENCODE_ANY | JSON_SORT_KEYS);
    int rc = blobref_hash (hash_name, (uint8_t *)s, strlen (s) + 1,
                           ref, sizeof (href_t));
    free (s);
    return rc;
}
