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

#include "treeobj.h"

struct kvsdir_struct {
    flux_t *handle;
    char *rootref; /* optional snapshot reference */
    char *key;
    json_t *dirobj;
    char *dirobj_string;
    int usecount;
};

struct kvsdir_iterator_struct {
    zlist_t *keys;
    bool reset;
};

void kvsdir_incref (kvsdir_t *dir)
{
    dir->usecount++;
}

void kvsdir_destroy (kvsdir_t *dir)
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

/* If rootref is non-NULL, the kvsdir records the root reference
 * so that subsequent kvsdir_get_* accesses can be relative to that
 * snapshot.  Otherwise, they are relative to the current root.
 */
kvsdir_t *kvsdir_create (flux_t *handle, const char *rootref,
                         const char *key, const char *json_str)
{
    kvsdir_t *dir;

    if (!key || !json_str) {
        errno = EINVAL;
        return NULL;
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
    if (!(dir->dirobj = json_loads (json_str, 0, NULL))) {
        errno = EINVAL;
        goto error;
    }
    if (treeobj_validate (dir->dirobj) < 0)
        goto error;
    if (!treeobj_is_dir (dir->dirobj)) {
        errno = EINVAL;
        goto error;
    }
    dir->usecount = 1;

    return dir;
error:
    kvsdir_destroy (dir);
    return NULL;
}

const char *kvsdir_tostring (kvsdir_t *dir)
{
    if (!dir->dirobj_string) {
        if (!(dir->dirobj_string = json_dumps (dir->dirobj, JSON_COMPACT))) {
            errno = ENOMEM;
            return NULL;
        }
    }
    return dir->dirobj_string;
}

int kvsdir_get_size (kvsdir_t *dir)
{
    return treeobj_get_count (dir->dirobj);
}

const char *kvsdir_key (kvsdir_t *dir)
{
    return dir->key;
}

void *kvsdir_handle (kvsdir_t *dir)
{
    return dir->handle;
}

const char *kvsdir_rootref (kvsdir_t *dir)
{
    return dir->rootref;
}

void kvsitr_destroy (kvsitr_t *itr)
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

kvsitr_t *kvsitr_create (kvsdir_t *dir)
{
    kvsitr_t *itr = NULL;
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
    kvsitr_destroy (itr);
    return NULL;
}

void kvsitr_rewind (kvsitr_t *itr)
{
    if (itr)
        itr->reset = true;
}

const char *kvsitr_next (kvsitr_t *itr)
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

bool kvsdir_exists (kvsdir_t *dir, const char *name)
{
    if (treeobj_get_entry (dir->dirobj, name))
        return true;
    return false;
}

bool kvsdir_isdir (kvsdir_t *dir, const char *name)
{
    json_t *obj = treeobj_get_entry (dir->dirobj, name);

    if (obj) {
        if (treeobj_is_dir (obj) || treeobj_is_dirref (obj))
            return true;
    }
    return false;
}

bool kvsdir_issymlink (kvsdir_t *dir, const char *name)
{
    json_t *obj = treeobj_get_entry (dir->dirobj, name);

    if (obj) {
        if (treeobj_is_symlink (obj))
            return true;
    }
    return false;
}


char *kvsdir_key_at (kvsdir_t *dir, const char *name)
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


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
