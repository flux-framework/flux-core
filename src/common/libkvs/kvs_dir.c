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


struct kvsdir_struct {
    flux_t *handle;
    char *rootref; /* optional snapshot reference */
    char *key;
    json_t *dirobj;
    char *dirobj_string;
    int usecount;
};

struct kvsdir_iterator_struct {
    json_t *dirobj;
    void *iter;
};

void kvsdir_incref (kvsdir_t *dir)
{
    dir->usecount++;
}

void kvsdir_destroy (kvsdir_t *dir)
{
    if (--dir->usecount == 0) {
        free (dir->rootref);
        free (dir->key);
        json_decref (dir->dirobj);
        free (dir);
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

    if (!(dir = calloc (1, sizeof (*dir))))
        goto nomem;

    dir->handle = handle;
    if (rootref) {
        if (!(dir->rootref = strdup (rootref)))
            goto nomem;
    }
    if (!(dir->key = strdup (key)))
        goto nomem;
    if (!(dir->dirobj = json_loads (json_str, 0, NULL)))
        goto nomem;
    dir->usecount = 1;

    return dir;
nomem:
    kvsdir_destroy (dir);
    errno = ENOMEM;
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
    return json_object_size (dir->dirobj);
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
        free (itr);
    }
}

kvsitr_t *kvsitr_create (kvsdir_t *dir)
{
    kvsitr_t *itr;

    if (!(itr = calloc (1, sizeof (*itr))))
        goto nomem;
    itr->dirobj = dir->dirobj;
    itr->iter = json_object_iter (itr->dirobj);

    return itr;
nomem:
    kvsitr_destroy (itr);
    errno = ENOMEM;
    return NULL;
}

void kvsitr_rewind (kvsitr_t *itr)
{
    itr->iter = json_object_iter (itr->dirobj);
}

const char *kvsitr_next (kvsitr_t *itr)
{
    const char *name = NULL;

    if (itr->iter) {
        name = json_object_iter_key (itr->iter);
        itr->iter = json_object_iter_next (itr->dirobj, itr->iter);
    }
    return name;
}

bool kvsdir_exists (kvsdir_t *dir, const char *name)
{
    if (json_object_get (dir->dirobj, name))
        return true;
    return false;
}

bool kvsdir_isdir (kvsdir_t *dir, const char *name)
{
    json_t *obj = json_object_get (dir->dirobj, name);

    if (obj) {
        if (json_object_get (obj, "DIRREF") || json_object_get (obj, "DIRVAL"))
            return true;
    }
    return false;
}

bool kvsdir_issymlink (kvsdir_t *dir, const char *name)
{
    json_t *obj = json_object_get (dir->dirobj, name);

    if (obj) {
        if (json_object_get (obj, "LINKVAL"))
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
