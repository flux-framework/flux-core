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

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"

#include "types.h"

json_object *json_object_copydir (json_object *dir)
{
    json_object *cpy;
    json_object_iter iter;

    if (!(cpy = json_object_new_object ()))
        oom ();
    json_object_object_foreachC (dir, iter) {
        json_object_get (iter.val);
        json_object_object_add (cpy, iter.key, iter.val);
    }
    return cpy;
}

bool json_compare (json_object *o1, json_object *o2)
{
    const char *s1 = json_object_to_json_string (o1);
    const char *s2 = json_object_to_json_string (o2);

    return !strcmp (s1, s2);
}

int json_hash (const char *hash_name, json_object *o, href_t ref)
{
    const char *s = json_object_to_json_string (o);

    if (blobref_hash (hash_name, (uint8_t *)s, strlen (s) + 1,
                      ref, sizeof (href_t)) < 0)
        return -1;
    return 0;
}
