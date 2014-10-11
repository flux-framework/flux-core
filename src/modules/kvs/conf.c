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
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"

const char *kvs_conf_root = "config";

static int load_one (flux_conf_t cf, kvsdir_t dir, const char *name)
{
    char *key = kvsdir_key_at (dir, name);
    char *skey = key + strlen (kvs_conf_root) + 1;
    char *val = NULL;
    int rc = -1;

    assert (strlen (kvs_conf_root) < strlen (key));

    if (kvsdir_get_string (dir, name, &val) == 0) { /* ignore if !string */
        if (flux_conf_put (cf, skey, val) < 0)
            goto done;
    }
    rc = 0;
done:
    if (val)
        free (val);
    free (key);
    return rc;
}

static int load_kvsdir (flux_conf_t cf, kvsdir_t dir)
{
    kvsitr_t itr;
    const char *name;
    int rc = -1;

    itr = kvsitr_create (dir);
    while ((name = kvsitr_next (itr))) {
        if (kvsdir_isdir (dir, name)) {
            kvsdir_t ndir;
            if (kvsdir_get_dir (dir, &ndir, "%s", name) < 0)
                goto done;
            if (load_kvsdir (cf, ndir) < 0)
                goto done;
            kvsdir_destroy (ndir);
        } else {
            if (load_one (cf, dir, name) < 0)
                goto done;
        }
    }
    rc = 0;
done:
    kvsitr_destroy (itr);
    return rc;
}

int kvs_conf_load (flux_t h, flux_conf_t cf)
{
    kvsdir_t dir = NULL;
    int rc = -1;

    flux_conf_clear (cf);
    if (kvs_get_dir (h, &dir, kvs_conf_root) < 0)
        goto done;
    if (load_kvsdir (cf, dir) < 0)
        goto done;
    rc = 0;
done:
    if (dir)
        kvsdir_destroy (dir);
    return rc;
}

int kvs_conf_save (flux_t h, flux_conf_t cf)
{
    flux_conf_itr_t itr = flux_conf_itr_create (cf);
    const char *key;
    int rc = -1;

    if (kvs_unlink (h, kvs_conf_root) < 0)
        goto done;
    if (kvs_commit (h) < 0)
        goto done;
    while ((key = flux_conf_next (itr))) {
        char *nkey = xasprintf ("%s.%s", kvs_conf_root, key);
        const char *val = flux_conf_get (cf, key);
        int n = kvs_put_string (h, nkey, val);
        free (nkey);
        if (n < 0)
            goto done;
    }       
    if (kvs_commit (h) < 0)
        goto done;
    rc = 0;
done:
    flux_conf_itr_destroy (itr);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
