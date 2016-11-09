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
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/blobref.h"

#include "json_dirent.h"

json_object *dirent_create (char *type, void *arg)
{
    json_object *dirent = Jnew ();
    bool valid_type = false;

    if (!strcmp (type, "FILEREF") || !strcmp (type, "DIRREF")) {
        char *ref = arg;

        Jadd_str (dirent, type, ref);
        valid_type = true;
    } else if (!strcmp (type, "FILEVAL") || !strcmp (type, "DIRVAL")
                                         || !strcmp (type, "LINKVAL")) {
        json_object *val = arg;

        if (val)
            json_object_get (val);
        else
            val = Jnew ();
        json_object_object_add (dirent, type, val);
        valid_type = true;
    }
    assert (valid_type == true);

    return dirent;
}

bool dirent_match (json_object *dirent1, json_object *dirent2)
{
    if (!dirent1 && !dirent2)
        return true;
    if ((dirent1 && !dirent2) || (!dirent1 && dirent2))
        return false;
    if (!strcmp (Jtostr (dirent1), Jtostr (dirent2)))
        return true;
    return false;
}

void dirent_append (json_object **array, const char *key, json_object *dirent)
{
    json_object *op = Jnew ();

    Jadd_str (op, "key", key);
    json_object_object_add (op, "dirent", dirent);
    if (!*array)
        *array = Jnew_ar ();
    json_object_array_add (*array, op);
}

int dirent_validate (json_object *dirent)
{
    json_object *o;

    if (!dirent)
        goto error;
    if ((o = Jobj_get (dirent, "DIRVAL"))) {
        json_object_iter iter;
        json_object_object_foreachC (o, iter) {
            if (dirent_validate (iter.val) < 0)
                goto error;
        }
    }
    else if ((o = Jobj_get (dirent, "FILEVAL"))) {
        /* Any json type is valid here */
    }
    else if ((o = Jobj_get (dirent, "LINKVAL"))) {
        if (json_object_get_type (o) != json_type_string)
            goto error;
    }
    else if ((o = Jobj_get (dirent, "DIRREF"))
            || (o = Jobj_get (dirent, "FILEREF"))) {
        if (json_object_get_type (o) != json_type_string)
            goto error;
        const char *s = json_object_get_string (o);
        uint8_t hash[BLOBREF_MAX_DIGEST_SIZE];
        if (blobref_strtohash (s, hash, sizeof (hash)) < 0)
            goto error;
    }
    else
        goto error;
    return 0;
error:
    errno = EINVAL;
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
