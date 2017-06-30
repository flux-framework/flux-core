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
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"

#include "commit.h"

static void commit_destroy (commit_t *c)
{
    if (c) {
        Jput (c->rootcpy);
        if (c->missing_refs)
            zlist_destroy (&c->missing_refs);
        if (c->dirty_cache_entries)
            zlist_destroy (&c->dirty_cache_entries);
        /* fence destroyed through management of fence, not commit_t's
         * responsibility */
        free (c);
    }
}

static commit_t *commit_create (fence_t *f, commit_mgr_t *cm)
{
    commit_t *c;

    if (!(c = calloc (1, sizeof (*c)))) {
        errno = ENOMEM;
        goto error;
    }
    c->f = f;
    if (!(c->missing_refs = zlist_new ())) {
        errno = ENOMEM;
        goto error;
    }
    if (!(c->dirty_cache_entries = zlist_new ())) {
        errno = ENOMEM;
        goto error;
    }
    c->cm = cm;
    c->state = COMMIT_STATE_INIT;
    return c;
error:
    commit_destroy (c);
    return NULL;
}

int commit_get_errnum (commit_t *c)
{
    return c->errnum;
}

fence_t *commit_get_fence (commit_t *c)
{
    return c->f;
}

void *commit_get_aux (commit_t *c)
{
    return c->cm->aux;
}

const char *commit_get_newroot_ref (commit_t *c)
{
    return c->newroot;
}

int commit_iter_missing_refs (commit_t *c, commit_ref_cb cb, void *data)
{
    const char *ref;
    int rc = 0;

    while ((ref = zlist_pop (c->missing_refs))) {
        if (cb (c, ref, data) < 0) {
            rc = -1;
            break;
        }
    }

    if (rc < 0)
        while ((ref = zlist_pop (c->missing_refs)));

    return rc;
}

int commit_iter_dirty_cache_entries (commit_t *c,
                                     commit_cache_entry_cb cb,
                                     void *data)
{
    struct cache_entry *hp;
    int rc = 0;

    while ((hp = zlist_pop (c->dirty_cache_entries))) {
        if (cb (c, hp, data) < 0) {
            rc = -1;
            break;
        }
    }

    if (rc < 0)
        while ((hp = zlist_pop (c->dirty_cache_entries)));

    return rc;
}

commit_mgr_t *commit_mgr_create (struct cache *cache,
                                 const char *hash_name,
                                 void *aux)
{
    commit_mgr_t *cm;

    if (!(cm = calloc (1, sizeof (*cm)))) {
        errno = ENOMEM;
        goto error;
    }
    cm->cache = cache;
    cm->hash_name = hash_name;
    if (!(cm->fences = zhash_new ())) {
        errno = ENOMEM;
        goto error;
    }
    if (!(cm->ready = zlist_new ())) {
        errno = ENOMEM;
        goto error;
    }
    cm->aux = aux;
    return cm;

 error:
    commit_mgr_destroy (cm);
    return NULL;
}

void commit_mgr_destroy (commit_mgr_t *cm)
{
    if (cm) {
        if (cm->fences)
            zhash_destroy (&cm->fences);
        if (cm->ready)
            zlist_destroy (&cm->ready);
        free (cm);
    }
}

int commit_mgr_add_fence (commit_mgr_t *cm, fence_t *f)
{
    const char *name;

    if (!Jget_ar_str (fence_get_json_names (f), 0, &name)) {
        errno = EINVAL;
        goto error;
    }
    if (zhash_insert (cm->fences, name, f) < 0) {
        errno = EEXIST;
        goto error;
    }
    zhash_freefn (cm->fences, name, (zhash_free_fn *)fence_destroy);
    return 0;
error:
    return -1;
}

fence_t *commit_mgr_lookup_fence (commit_mgr_t *cm, const char *name)
{
    return zhash_lookup (cm->fences, name);
}

int commit_mgr_process_fence_request (commit_mgr_t *cm, fence_t *f)
{
    if (fence_count_reached (f)) {
        commit_t *c;

        if (!(c = commit_create (f, cm)))
            return -1;

        if (zlist_append (cm->ready, c) < 0)
            oom ();
        zlist_freefn (cm->ready, c, (zlist_free_fn *)commit_destroy, true);
    }

    return 0;
}

bool commit_mgr_commits_ready (commit_mgr_t *cm)
{
    commit_t *c;

    if ((c = zlist_first (cm->ready)) && !c->blocked)
        return true;
    return false;
}

commit_t *commit_mgr_get_ready_commit (commit_mgr_t *cm)
{
    if (commit_mgr_commits_ready (cm))
        return zlist_first (cm->ready);
    return NULL;
}

void commit_mgr_remove_commit (commit_mgr_t *cm, commit_t *c)
{
    zlist_remove (cm->ready, c);
}

void commit_mgr_remove_fence (commit_mgr_t *cm, const char *name)
{
    zhash_delete (cm->fences, name);
}

int commit_mgr_get_noop_stores (commit_mgr_t *cm)
{
    return cm->noop_stores;
}

void commit_mgr_clear_noop_stores (commit_mgr_t *cm)
{
    cm->noop_stores = 0;
}

/* Merge ready commits that are mergeable, where merging consists of
 * popping the "donor" commit off the ready list, and appending its
 * ops to the top commit.  The top commit can be appended to if it
 * hasn't started, or is still building the rootcpy, e.g. stalled
 * walking the namespace.
 *
 * Break when an unmergeable commit is discovered.  We do not wish to
 * merge non-adjacent fences, as it can create undesireable out of
 * order scenarios.  e.g.
 *
 * commit #1 is mergeable:     set A=1
 * commit #2 is non-mergeable: set A=2
 * commit #3 is mergeable:     set A=3
 *
 * If we were to merge commit #1 and commit #3, A=2 would be set after
 * A=3.
 */
void commit_mgr_merge_ready_commits (commit_mgr_t *cm)
{
    commit_t *c = zlist_first (cm->ready);

    /* commit must still be in state where merged in ops can be
     * applied */
    if (c
        && c->errnum == 0
        && c->state <= COMMIT_STATE_APPLY_OPS
        && !(fence_get_flags (c->f) & KVS_NO_MERGE)) {
        commit_t *nc;
        nc = zlist_pop (cm->ready);
        assert (nc == c);
        while ((nc = zlist_first (cm->ready))) {

            /* if return == 0, we've merged as many as we currently
             * can */
            if (!fence_merge (c->f, nc->f))
                break;

            /* Merged fence, remove off ready list */
            zlist_remove (cm->ready, nc);
        }
        if (zlist_push (cm->ready, c) < 0)
            oom ();
        zlist_freefn (cm->ready, c, (zlist_free_fn *)commit_destroy, false);
    }
}
