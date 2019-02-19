/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* restart - reload active jobs from the KVS */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <argz.h>
#include <envz.h>

#include "src/common/libjob/job.h"
#include "src/common/libutil/fluid.h"

#include "job.h"
#include "restart.h"
#include "util.h"

int restart_count_char (const char *s, char c)
{
    int count = 0;
    while (*s) {
        if (*s++ == c)
            count++;
    }
    return count;
}

static int depthfirst_map_one (flux_t *h, const char *key, int dirskip,
                               restart_map_f cb, void *arg)
{
    flux_jobid_t id;
    flux_future_t *f;
    const char *eventlog;
    struct job *job = NULL;
    int rc = -1;

    if (strlen (key) <= dirskip) {
        errno = EINVAL;
        return -1;
    }
    if (fluid_decode (key + dirskip + 1, &id, FLUID_STRING_DOTHEX) < 0)
        return -1;
    if (!(f = util_attr_lookup (h, id, true, 0, "eventlog")))
        goto done;
    if (flux_kvs_lookup_get (f, &eventlog) < 0)
        goto done;
    if (!(job = job_create_from_eventlog (id, eventlog)))
        goto done;
    if (cb (job, arg) < 0)
        goto done;
    rc = 1;
done:
    flux_future_destroy (f);
    job_decref (job);
    return rc;
}

static int depthfirst_map (flux_t *h, const char *key,
                           int dirskip, restart_map_f cb, void *arg)
{
    flux_future_t *f;
    const flux_kvsdir_t *dir;
    flux_kvsitr_t *itr;
    const char *name;
    int path_level;
    int count = 0;
    int rc = -1;

    path_level = restart_count_char (key + dirskip, '.');
    if (!(f = flux_kvs_lookup (h, NULL, FLUX_KVS_READDIR, key)))
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
            n = depthfirst_map_one (h, nkey, dirskip, cb, arg);
        else
            n = depthfirst_map (h, nkey, dirskip, cb, arg);
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

int restart_map (flux_t *h, restart_map_f cb, void *arg)
{
    const char *dirname = "job.active";

    return depthfirst_map (h, dirname, strlen (dirname), cb, arg);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
