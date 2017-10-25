/*****************************************************************************\
 *  Copyright (c) 2017 Lawrence Livermore National Security, LLC.  Produced at
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
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <jansson.h>
#include <czmq.h>
#include <flux/core.h>

#include "kvs_dir_private.h"
#include "treeobj.h"

struct flux_kvsdir {
    flux_t *handle;
    char *rootref; /* optional snapshot reference */
    char *key;
    json_t *dirobj;
    int usecount;
};

struct flux_kvsitr {
    zlist_t *keys;
    bool reset;
};

void flux_kvsdir_incref (flux_kvsdir_t *dir)
{
    dir->usecount++;
}

void flux_kvsdir_destroy (flux_kvsdir_t *dir)
{
    if (dir && --dir->usecount == 0) {
        int saved_errno = errno;
        free (dir->rootref);
        free (dir->key);
        json_decref (dir->dirobj);
        free (dir);
        errno = saved_errno;
    }
}

flux_kvsdir_t *flux_kvsdir_copy (const flux_kvsdir_t *dir)
{
    return kvsdir_create_fromobj (dir->handle, dir->rootref,
                                  dir->key, dir->dirobj);
}

/* If rootref is non-NULL, the kvsdir records the root reference
 * so that subsequent flux_kvsdir_get_* accesses can be relative to that
 * snapshot.  Otherwise, they are relative to the current root.
 */
flux_kvsdir_t *flux_kvsdir_create (flux_t *h, const char *rootref,
                                   const char *key, const char *json_str)
{
    flux_kvsdir_t *dir = NULL;
    json_t *dirobj = NULL;

    if (!key || !json_str || !(dirobj = treeobj_decode (json_str))) {
        errno = EINVAL;
        goto done;
    }
    if (!(dir = kvsdir_create_fromobj (h, rootref, key, dirobj)))
        goto done;
done:
    json_decref (dirobj);
    return dir;
}

int flux_kvsdir_get_size (const flux_kvsdir_t *dir)
{
    return treeobj_get_count (dir->dirobj);
}

const char *flux_kvsdir_key (const flux_kvsdir_t *dir)
{
    return dir->key;
}

void *flux_kvsdir_handle (const flux_kvsdir_t *dir)
{
    return dir->handle;
}

const char *flux_kvsdir_rootref (const flux_kvsdir_t *dir)
{
    return dir->rootref;
}

void flux_kvsitr_destroy (flux_kvsitr_t *itr)
{
    if (itr) {
        int saved_errno = errno;
        zlist_destroy (&itr->keys);
        free (itr);
        errno = saved_errno;
    }
}

static int sort_cmp (void *item1, void *item2)
{
    if (!item1 && item2)
        return -1;
    if (!item1 && !item2)
        return 0;
    if (item1 && !item2)
        return 1;
    return strcmp (item1, item2);
}

flux_kvsitr_t *flux_kvsitr_create (const flux_kvsdir_t *dir)
{
    flux_kvsitr_t *itr = NULL;
    const char *key;
    json_t *dirdata, *value;

    if (!dir) {
        errno = EINVAL;
        goto error;
    }
    if (!(itr = calloc (1, sizeof (*itr))))
        goto error;
    if (!(itr->keys = zlist_new ()))
        goto error;
    dirdata = treeobj_get_data (dir->dirobj);
    json_object_foreach (dirdata, key, value) {
        if (zlist_push (itr->keys, (char *)key) < 0)
            goto error;
    }
    zlist_sort (itr->keys, sort_cmp);
    itr->reset = true;
    return itr;
error:
    flux_kvsitr_destroy (itr);
    return NULL;
}

void flux_kvsitr_rewind (flux_kvsitr_t *itr)
{
    if (itr)
        itr->reset = true;
}

const char *flux_kvsitr_next (flux_kvsitr_t *itr)
{
    const char *name = NULL;

    if (itr) {
        if (itr->reset)
            name = zlist_first (itr->keys);
        else
            name = zlist_next (itr->keys);
        if (name)
            itr->reset = false;
    }
    return name;
}

bool flux_kvsdir_exists (const flux_kvsdir_t *dir, const char *name)
{
    if (treeobj_get_entry (dir->dirobj, name))
        return true;
    return false;
}

bool flux_kvsdir_isdir (const flux_kvsdir_t *dir, const char *name)
{
    json_t *obj = treeobj_get_entry (dir->dirobj, name);

    if (obj) {
        if (treeobj_is_dir (obj) || treeobj_is_dirref (obj))
            return true;
    }
    return false;
}

bool flux_kvsdir_issymlink (const flux_kvsdir_t *dir, const char *name)
{
    json_t *obj = treeobj_get_entry (dir->dirobj, name);

    if (obj) {
        if (treeobj_is_symlink (obj))
            return true;
    }
    return false;
}


char *flux_kvsdir_key_at (const flux_kvsdir_t *dir, const char *name)
{
    char *s;

    if (!strcmp (dir->key, ".")) {
        if (!(s = strdup (name)))
            goto nomem;
    }
    else {
        if (asprintf (&s, "%s.%s", dir->key, name) < 0)
            goto nomem;
    }
    return s;
nomem:
    errno = ENOMEM;
    return NULL;
}

/* kvs_txn_private.h */

flux_kvsdir_t *kvsdir_create_fromobj (flux_t *handle, const char *rootref,
                                      const char *key, json_t *treeobj)
{
    flux_kvsdir_t *dir = NULL;

    if (!key || !treeobj || treeobj_validate (treeobj) < 0
                         || !treeobj_is_dir (treeobj)) {
        errno = EINVAL;
        goto error;
    }
    if (!(dir = calloc (1, sizeof (*dir))))
        goto error;

    dir->handle = handle;
    if (rootref) {
        if (!(dir->rootref = strdup (rootref)))
            goto error;
    }
    if (!(dir->key = strdup (key)))
        goto error;
    dir->dirobj = json_incref (treeobj);
    dir->usecount = 1;

    return dir;
error:
    flux_kvsdir_destroy (dir);
    return NULL;
}

json_t *kvsdir_get_obj (flux_kvsdir_t *dir)
{
    return dir->dirobj;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
