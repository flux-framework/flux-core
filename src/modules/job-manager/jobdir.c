/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.  Additionally, the libflux-core library may be
 *  redistributed under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation, either version 2 of the license,
 *  or (at your option) any later version.
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
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libjob/job.h"
#include "src/common/libutil/fluid.h"

#include "jobdir.h"

static int count_char (const char *s, char c)
{
    int count = 0;
    while (*s) {
        if (*s++ == c)
            count++;
    }
    return count;
}

static int jobdir_depthfirst_map_one (flux_t *h, const char *key, int dirskip,
                                      jobdir_map_f cb, void *arg)
{
    flux_jobid_t id;
    char *nkey;
    flux_future_t *f = NULL;
    uint32_t userid;
    int priority;
    int saved_errno;

    if (strlen (key) <= dirskip) {
        errno = EINVAL;
        return -1;
    }
    if (fluid_decode (key + dirskip + 1, &id, FLUID_STRING_DOTHEX) < 0)
        return -1;
    /* userid */
    if (asprintf (&nkey, "%s.userid", key) < 0)
        return -1;
    if (!(f = flux_kvs_lookup (h, 0, nkey)))
        goto error;
    if (flux_kvs_lookup_get_unpack (f, "i", &userid) < 0)
        goto error;
    flux_future_destroy (f);
    free (nkey);
    f = NULL;
    nkey = NULL;

    /* priority */
    if (asprintf (&nkey, "%s.priority", key) < 0)
        return -1;
    if (!(f = flux_kvs_lookup (h, 0, nkey)))
        goto error;
    if (flux_kvs_lookup_get_unpack (f, "i", &priority) < 0)
        goto error;
    if (cb (id, priority, userid, arg) < 0)
        goto error;
    flux_future_destroy (f);
    free (nkey);

    if (cb (id, priority, userid, arg) < 0)
        goto error;
    return 1; // processed one
error:
    saved_errno = errno;
    flux_future_destroy (f);
    free (nkey);
    errno = saved_errno;
    return -1;
}

static int jobdir_depthfirst_map (flux_t *h, const char *key,
                                  int dirskip, jobdir_map_f cb, void *arg)
{
    flux_future_t *f;
    const flux_kvsdir_t *dir;
    flux_kvsitr_t *itr;
    const char *name;
    int path_level;
    int count = 0;
    int rc = -1;

    path_level = count_char (key + dirskip, '.');
    if (!(f = flux_kvs_lookup (h, FLUX_KVS_READDIR, key)))
        return -1;
    if (flux_kvs_lookup_get_dir (f, &dir) < 0) {
        if (errno == ENOENT && path_level == 0)
            rc = 0;
        goto done;
    }
    if (!(itr = flux_kvsitr_create (dir)))
        goto done;
    while ((name = flux_kvsitr_next (itr))) {
        char *nkey;
        int n;
        if (!flux_kvsdir_isdir (dir, name))
            continue;
        if (!(nkey = flux_kvsdir_key_at (dir, name)))
            goto done_destroyitr;
        if (path_level == 3) // orig 'key' = .A.B.C, thus 'nkey' is complete
            n = jobdir_depthfirst_map_one (h, nkey, dirskip, cb, arg);
        else
            n = jobdir_depthfirst_map (h, nkey, dirskip, cb, arg);
        if (n < 0) {
            int saved_errno = errno;
            free (nkey);
            errno = saved_errno;
            goto done_destroyitr;
        }
        count += n;
        free (nkey);
    }
    rc = count;
done_destroyitr:
    flux_kvsitr_destroy (itr);
done:
    flux_future_destroy (f);
    return rc;
}

int jobdir_map (flux_t *h, const char *dirname, jobdir_map_f cb, void *arg)
{
    return jobdir_depthfirst_map (h, dirname, strlen (dirname), cb, arg);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
