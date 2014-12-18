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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <ctype.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/nodeset.h"
#include "src/common/libmrpc/mrpc.h"

#include "modctl.h"
#include "proto.h"

typedef struct {
    char *name;
    int size;
    char *digest;
    int idle;
    nodeset_t nodeset;
} module_t;

static void module_destroy (module_t *m)
{
    if (m->name)
        free (m->name);
    if (m->digest)
        free (m->digest);
    if (m->nodeset)
        nodeset_destroy (m->nodeset);
    free (m);
}

static module_t *module_create (const char *name, int size, const char *digest,
                                int idle, uint32_t nodeid)
{
    module_t *m = xzmalloc (sizeof (*m));

    m->name = xstrdup (name);
    m->size = size;
    m->digest = xstrdup (digest);
    m->idle = idle;
    if (!(m->nodeset = nodeset_new_rank (nodeid))) {
        module_destroy (m);
        errno = EPROTO;
        return NULL;
    }
    return m;
}

static int module_update (module_t *m, int idle, uint32_t nodeid)
{
    if (idle < m->idle)
        m->idle = idle;
    if (!nodeset_add_rank (m->nodeset, nodeid)) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int get_rlist_result (zhash_t *mods, flux_mrpc_t mrpc,
                             uint32_t nodeid, int *ep)
{
    JSON o = NULL;
    int rc = -1;
    const char *name, *digest;
    int i, len, errnum, size, idle;
    module_t *m;

    if (flux_mrpc_get_outarg (mrpc, nodeid, &o) < 0)
        goto done;
    if (modctl_rlist_dec (o, &errnum, &len) < 0)
        goto done;
    for (i = 0; i < len; i++) {
        if (modctl_rlist_dec_nth (o, i, &name, &size, &digest, &idle) < 0)
            goto done;
        if (!(m = zhash_lookup (mods, digest))) {
            if (!(m = module_create (name, size, digest, idle, nodeid)))
                goto done;
            zhash_update (mods, digest, m);
            zhash_freefn (mods, digest, (zhash_free_fn *)module_destroy);
        } else if (module_update (m, idle, nodeid) < 0)
            goto done;
    }
    *ep = errnum;
    rc = 0;
done:
    Jput (o);
    return rc;
}

static int cb_rlist_result (zhash_t *mods, flux_lsmod_f cb, void *arg)
{
    zlist_t *keys = zhash_keys (mods);
    char *key;
    int rc = -1;

    if (!keys) {
        errno = ENOMEM;
        goto done;
    }
    key = zlist_first (keys);
    while (key) {
        module_t *m = zhash_lookup (mods, key);
        if (m) {
            const char *ns = nodeset_str (m->nodeset);
            if (cb (m->name, m->size, m->digest, m->idle, ns, arg) < 0)
                goto done;
        }
        key = zlist_next (keys);
    }
    rc = 0;
done:
    zlist_destroy (&keys);
    return rc;
}

int flux_modctl_list (flux_t h, const char *svc, const char *nodeset,
                      flux_lsmod_f cb, void *arg)
{
    JSON in = NULL;
    int rc = -1;
    uint32_t nodeid;
    flux_mrpc_t mrpc = NULL;
    int errnum = 0;
    zhash_t *mods = NULL;

    if (!h || !cb) {
        errno = EINVAL;
        goto done;
    }
    if (!(mrpc = flux_mrpc_create (h, nodeset)))
        goto done;
    if (!(in = modctl_tlist_enc (svc)))
        goto done;
    flux_mrpc_put_inarg (mrpc, in);
    if (flux_mrpc (mrpc, "modctl.list") < 0)
        goto done;
    flux_mrpc_rewind_outarg (mrpc);
    if (!(mods = zhash_new ()))
        oom ();
    while ((nodeid = flux_mrpc_next_outarg (mrpc)) != -1) {
        if (get_rlist_result (mods, mrpc, nodeid, &errnum) < 0)
            goto done;
        if (errnum)
            goto done;
    }
    if (cb_rlist_result (mods, cb, arg) < 0)
        goto done;
    rc = 0;
done:
    zhash_destroy (&mods);
    if (mrpc)
        flux_mrpc_destroy (mrpc);
    Jput (in);
    if (errnum)
        errno = errnum;
    return rc;
}

static int get_rload_errnum (flux_mrpc_t mrpc, uint32_t nodeid, int *ep)
{
    JSON o = NULL;
    int rc = -1;
    int errnum;

    if (flux_mrpc_get_outarg (mrpc, nodeid, &o) < 0)
        goto done;
    if (modctl_rload_dec (o, &errnum) < 0)
        goto done;
    *ep = errnum;
    rc = 0;
done:
    Jput (o);
    return rc;
}

int flux_modctl_load (flux_t h, const char *nodeset, const char *path,
                      int argc, char **argv)
{
    int rc = -1;
    JSON in = NULL;
    flux_mrpc_t mrpc = NULL;
    uint32_t nodeid;
    int errnum = 0;

    if (!h) {
        errno = EINVAL;
        goto done;
    }
    if (!(mrpc = flux_mrpc_create (h, nodeset)))
        goto done;
    if (!(in = modctl_tload_enc (path, argc, argv)))
        goto done;
    flux_mrpc_put_inarg (mrpc, in);
    if (flux_mrpc (mrpc, "modctl.load") < 0)
        goto done;
    flux_mrpc_rewind_outarg (mrpc);
    while ((nodeid = flux_mrpc_next_outarg (mrpc)) != -1) {
        if (get_rload_errnum (mrpc, nodeid, &errnum) < 0)
            goto done;
        if (errnum)
            goto done;
    }
    rc = 0;
done:
    if (mrpc)
        flux_mrpc_destroy (mrpc);
    Jput (in);
    if (errnum)
        errno = errnum;
    return rc;
}

static int get_runload_errnum (flux_mrpc_t mrpc, uint32_t nodeid, int *ep)
{
    JSON o = NULL;
    int rc = -1;
    int errnum;

    if (flux_mrpc_get_outarg (mrpc, nodeid, &o) < 0)
        goto done;
    if (modctl_runload_dec (o, &errnum) < 0)
        goto done;
    *ep = errnum;
    rc = 0;
done:
    Jput (o);
    return rc;
}

int flux_modctl_unload (flux_t h, const char *nodeset, const char *name)
{
    flux_mrpc_t mrpc = NULL;
    JSON in = NULL;
    int rc = -1;
    uint32_t nodeid;
    int errnum = 0;
    int successes = 0, missing = 0;

    if (!h) {
        errno = EINVAL;
        goto done;
    }
    if (!(mrpc = flux_mrpc_create (h, nodeset)))
        goto done;
    if (!(in = modctl_tunload_enc (name)))
        goto done;
    flux_mrpc_put_inarg (mrpc, in);
    if (flux_mrpc (mrpc, "modctl.unload") < 0)
        goto done;
    flux_mrpc_rewind_outarg (mrpc);
    while ((nodeid = flux_mrpc_next_outarg (mrpc)) != -1) {
        if (get_runload_errnum (mrpc, nodeid, &errnum) < 0)
            goto done;
        if (errnum == 0)
            successes++;
        else if (errnum == ENOENT)
            missing++;
        else if (errnum)
            goto done;
    }
    /* ignore ENOENT unless nothing worked
     */
    if (successes == 0 && missing > 0) {
        errnum = ENOENT;
        goto done;
    }
    rc = 0;
done:
    if (mrpc)
        flux_mrpc_destroy (mrpc);
    Jput (in);
    if (rc < 0 && errnum > 0)
        errno = errnum;
    return rc;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
