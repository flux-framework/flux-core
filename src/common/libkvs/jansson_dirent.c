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
#include <jansson.h>

#include "src/common/libutil/blobref.h"

#include "jansson_dirent.h"

json_t *j_dirent_create (const char *type, void *arg)
{
    json_t *dirent = NULL;
    bool valid_type = false;

    if (!(dirent = json_object ())) {
        errno = ENOMEM;
        goto error;
    }

    if (!strcmp (type, "FILEREF") || !strcmp (type, "DIRREF")) {
        char *ref = arg;
        json_t *o;

        if (!(o = json_string (ref))) {
            errno = ENOMEM;
            goto error;
        }
        if (json_object_set_new (dirent, type, o) < 0) {
            json_decref (o);
            errno = ENOMEM;
            goto error;
        }

        valid_type = true;
    } else if (!strcmp (type, "FILEVAL") || !strcmp (type, "DIRVAL")
                                         || !strcmp (type, "LINKVAL")) {
        json_t *val = arg;

        if (val)
            json_incref (val);
        else {
            if (!(val = json_object ())) {
                errno = ENOMEM;
                goto error;
            }
        }
        if (json_object_set_new (dirent, type, val) < 0) {
            json_decref (val);
            errno = ENOMEM;
            goto error;
        }
        valid_type = true;
    }
    assert (valid_type == true);

    return dirent;

error:
    json_decref (dirent);
    return NULL;
}

int j_dirent_validate (json_t *dirent)
{
    json_t *o;

    if (!dirent)
        goto error;
    if ((o = json_object_get (dirent, "DIRVAL"))) {
        const char *key;
        json_t *val;
        json_object_foreach (o, key, val) {
            if (j_dirent_validate (val) < 0)
                goto error;
        }
    }
    else if ((o = json_object_get (dirent, "FILEVAL"))) {
        if (json_typeof (o) == JSON_NULL)
            goto error;
    }
    else if ((o = json_object_get (dirent, "LINKVAL"))) {
        if (json_typeof (o) != JSON_STRING)
            goto error;
    }
    else if ((o = json_object_get (dirent, "DIRREF"))
            || (o = json_object_get (dirent, "FILEREF"))) {
        if (json_typeof (o) != JSON_STRING)
            goto error;
        const char *s = json_string_value (o);
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
