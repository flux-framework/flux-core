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
#include <jansson.h>

#include "src/common/libutil/blobref.h"
#include "src/common/libkvs/treeobj.h"

#include "cache.h"
#include "kvs_util.h"

#include "lookup.h"

/* Break cycles in symlink references.
 */
#define SYMLINK_CYCLE_LIMIT 10

#define LOOKUP_MAGIC 0x15151515

typedef struct {
    int depth;
    char *path_copy;            /* for internal parsing, do not use */
    const json_t *dirent;
    zlist_t *pathcomps;
} walk_level_t;

struct lookup {
    int magic;

    /* inputs from user */
    struct cache *cache;
    int current_epoch;

    char *root_dir;
    char *root_ref;
    char *root_ref_copy;

    char *path;

    flux_t *h;

    int flags;

    void *aux;

    /* potential return values from lookup */
    json_t *val;           /* value of lookup */

    /* if valref_missing_refs is true, iterate on refs, else
     * return missing_ref string.
     */
    const json_t *valref_missing_refs;
    const char *missing_ref;

    int errnum;                 /* errnum if error */
    int aux_errnum;

    /* API internal */
    json_t *root_dirent;
    zlist_t *levels;
    const json_t *wdirent;       /* result after walk() */
    enum {
        LOOKUP_STATE_INIT,
        LOOKUP_STATE_CHECK_ROOT,
        LOOKUP_STATE_WALK,
        LOOKUP_STATE_VALUE,
        LOOKUP_STATE_FINISHED,
    } state;
};

static bool last_pathcomp (zlist_t *pathcomps, const void *data)
{
    return (zlist_tail (pathcomps) == data);
}

/* Create list of path components, i.e. in path "a.b.c", create list
 * of "a", "b", and "c".
 */
static zlist_t *walk_pathcomps_zlist_create (walk_level_t *wl)
{
    char *next, *current;
    zlist_t *pathcomps = NULL;
    int saved_errno;

    if (!(pathcomps = zlist_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }

    /* N.B. not creating memory, placing pointer to path components,
     * so cleanup is simply list destroy.
     */

    current = wl->path_copy;
    while ((next = strchr (current, '.'))) {
        *next++ = '\0';

        if (zlist_append (pathcomps, current) < 0) {
            saved_errno = ENOMEM;
            goto error;
        }

        current = next;
    }

    if (zlist_append (pathcomps, current) < 0) {
        saved_errno = ENOMEM;
        goto error;
    }

    return pathcomps;

 error:
    zlist_destroy (&pathcomps);
    errno = saved_errno;
    return NULL;
}

static void walk_level_destroy (void *data)
{
    walk_level_t *wl = (walk_level_t *)data;
    if (wl) {
        zlist_destroy (&wl->pathcomps);
        free (wl->path_copy);
        free (wl);
    }
}

static walk_level_t *walk_level_create (const char *path,
                                        json_t *root_dirent,
                                        int depth)
{
    walk_level_t *wl = calloc (1, sizeof (*wl));
    int saved_errno;

    if (!wl) {
        saved_errno = ENOMEM;
        goto error;
    }
    if (!(wl->path_copy = strdup (path))) {
        saved_errno = ENOMEM;
        goto error;
    }
    wl->depth = depth;
    wl->dirent = root_dirent;
    if (!(wl->pathcomps = walk_pathcomps_zlist_create (wl))) {
        saved_errno = errno;
        goto error;
    }

    return wl;

 error:
    walk_level_destroy (wl);
    errno = saved_errno;
    return NULL;
}

static walk_level_t *walk_levels_push (lookup_t *lh,
                                       const char *path,
                                       int depth)
{
    walk_level_t *wl;

    if (!(wl = walk_level_create (path, lh->root_dirent, depth)))
        return NULL;

    if (zlist_push (lh->levels, wl) < 0) {
        walk_level_destroy (wl);
        errno = ENOMEM;
        return NULL;
    }
    zlist_freefn (lh->levels, wl, walk_level_destroy, false);

    return wl;
}

/* Get dirent of the requested path starting at the given root.
 *
 * Return true on success or error, error code is returned in ep and
 * should be checked upon return.
 *
 * Return false if path cannot be resolved.  Return missing reference
 * in load ref, which caller should then use to load missing reference
 * into KVS cache.
 */
static bool walk (lookup_t *lh)
{
    const json_t *dir;
    walk_level_t *wl = NULL;
    char *pathcomp;

    wl = zlist_head (lh->levels);

    /* walk directories */
    while ((pathcomp = zlist_head (wl->pathcomps))) {

        /* Get directory of dirent */

        if (treeobj_is_dirref (wl->dirent)) {
            struct cache_entry *hp;
            const char *refstr;
            int refcount;

            if ((refcount = treeobj_get_count (wl->dirent)) < 0) {
                lh->errnum = errno;
                goto error;
            }

            if (refcount != 1) {
                flux_log (lh->h, LOG_ERR, "invalid dirref count: %d", refcount);
                lh->errnum = ENOTRECOVERABLE;
                goto error;
            }

            if (!(refstr = treeobj_get_blobref (wl->dirent, 0))) {
                lh->errnum = errno;
                goto error;
            }

            if (!(hp = cache_lookup (lh->cache, refstr, lh->current_epoch))
                || !cache_entry_get_valid (hp)) {
                lh->missing_ref = refstr;
                goto stall;
            }
            if (!(dir = cache_entry_get_treeobj (hp))) {
                /* dirref pointed to non treeobj error, special case when
                 * root_dirent is bad, is EINVAL from user.
                 */
                flux_log (lh->h, LOG_ERR, "dirref points to non-treeobj");
                if (wl->depth == 0 && wl->dirent == lh->root_dirent)
                    lh->errnum = EINVAL;
                else
                    lh->errnum = ENOTRECOVERABLE;
                goto error;
            }
            if (!treeobj_is_dir (dir)) {
                /* dirref pointed to non-dir error, special case when
                 * root_dirent is bad, is EINVAL from user.
                 */
                if (wl->depth == 0 && wl->dirent == lh->root_dirent)
                    lh->errnum = EINVAL;
                else
                    lh->errnum = ENOTRECOVERABLE;
                goto error;
            }
        } else {
            /* Unexpected dirent type */
            if (treeobj_is_valref (wl->dirent)
                || treeobj_is_val (wl->dirent)) {
                /* don't return ENOENT or ENOTDIR, error to be
                 * determined by caller */
                goto error;
            }
            else {
                char *s = json_dumps (wl->dirent, 0);
                flux_log (lh->h, LOG_ERR,
                          "%s: unknown/unexpected dirent type: "
                          "lh->path=%s pathcomp=%s: wl->dirent=%s ",
                          __FUNCTION__, lh->path, pathcomp, s);
                free (s);
                lh->errnum = ENOTRECOVERABLE;
                goto error;
            }
        }

        /* Get directory reference of path component from directory */

        if (!(wl->dirent = treeobj_peek_entry (dir, pathcomp))) {
            /* if entry does not exist, not necessarily ENOENT error,
             * let caller decide.  If error not ENOENT, return to
             * caller. */
            if (errno != ENOENT)
                lh->errnum = errno;
            goto error;
        }

        /* Resolve dirent if it is a link */

        if (treeobj_is_symlink (wl->dirent)) {
            const char *linkstr;

            if (!(linkstr = treeobj_get_symlink (wl->dirent))) {
                lh->errnum = errno;
                goto error;
            }

            /* Follow link if in middle of path or if end of path,
             * flags say we can follow it
             */
            if (!last_pathcomp (wl->pathcomps, pathcomp)
                || (!(lh->flags & FLUX_KVS_READLINK)
                    && !(lh->flags & FLUX_KVS_TREEOBJ))) {

                if (wl->depth == SYMLINK_CYCLE_LIMIT) {
                    lh->errnum = ELOOP;
                    goto error;
                }

                /* "recursively" determine link dirent */
                if (!(wl = walk_levels_push (lh,
                                             linkstr,
                                             wl->depth + 1))) {
                    lh->errnum = errno;
                    goto error;
                }

                continue;
            }
        }

        if (last_pathcomp (wl->pathcomps, pathcomp)
            && wl->depth) {
            /* Unwind "recursive" step */
            do {
                walk_level_t *wl_tmp;
                char *pathcomp_tmp;

                /* Take current level off the top of the stack */
                zlist_pop (lh->levels);

                wl_tmp = zlist_head (lh->levels);
                pathcomp_tmp = zlist_head (wl_tmp->pathcomps);

                wl_tmp->dirent = wl->dirent;

                walk_level_destroy (wl);

                /* Set new current level */
                wl = wl_tmp;
                pathcomp = pathcomp_tmp;
            } while (wl->depth && last_pathcomp (wl->pathcomps, pathcomp));
        }

        zlist_remove (wl->pathcomps, pathcomp);
    }
    lh->wdirent = wl->dirent;
    return true;
error:
    lh->wdirent = NULL;
    return true;
stall:
    return false;
}

lookup_t *lookup_create (struct cache *cache,
                         int current_epoch,
                         const char *root_dir,
                         const char *root_ref,
                         const char *path,
                         flux_t *h,
                         int flags)
{
    lookup_t *lh = NULL;
    int saved_errno;

    if (!cache || !root_dir || !path) {
        errno = EINVAL;
        return NULL;
    }

    if (!(lh = calloc (1, sizeof (*lh)))) {
        saved_errno = ENOMEM;
        goto cleanup;
    }

    lh->magic = LOOKUP_MAGIC;
    lh->cache = cache;
    lh->current_epoch = current_epoch;
    /* must duplicate these strings, user may not keep pointer
     * alive */
    if (!(lh->root_dir = strdup (root_dir))) {
        saved_errno = ENOMEM;
        goto cleanup;
    }
    if (root_ref) {
        if (!(lh->root_ref_copy = strdup (root_ref))) {
            saved_errno = ENOMEM;
            goto cleanup;
        }
        lh->root_ref = lh->root_ref_copy;
    }
    else {
        lh->root_ref_copy = NULL;
        lh->root_ref = lh->root_dir;
    }
    if (!(lh->path = kvs_util_normalize_key (path, NULL))) {
        saved_errno = ENOMEM;
        goto cleanup;
    }
    lh->h = h;
    lh->flags = flags;

    lh->aux = NULL;

    lh->val = NULL;
    lh->valref_missing_refs = NULL;
    lh->missing_ref = NULL;
    lh->errnum = 0;

    if (!(lh->root_dirent = treeobj_create_dirref (lh->root_ref))) {
        saved_errno = errno;
        goto cleanup;
    }

    if (!(lh->levels = zlist_new ())) {
        saved_errno = ENOMEM;
        goto cleanup;
    }

    /* first depth is level 0 */
    if (!walk_levels_push (lh, lh->path, 0)) {
        saved_errno = errno;
        goto cleanup;
    }

    lh->wdirent = NULL;
    lh->state = LOOKUP_STATE_INIT;

    return lh;

 cleanup:
    lookup_destroy (lh);
    errno = saved_errno;
    return NULL;
}

void lookup_destroy (lookup_t *lh)
{
    if (lh && lh->magic == LOOKUP_MAGIC) {
        free (lh->root_dir);
        free (lh->root_ref_copy);
        free (lh->path);
        json_decref (lh->val);
        json_decref (lh->root_dirent);
        zlist_destroy (&lh->levels);
        lh->magic = ~LOOKUP_MAGIC;
        free (lh);
    }
}

bool lookup_validate (lookup_t *lh)
{
    if (lh && lh->magic == LOOKUP_MAGIC)
        return true;
    return false;
}

int lookup_get_errnum (lookup_t *lh)
{
    if (lh && lh->magic == LOOKUP_MAGIC) {
        if (lh->state == LOOKUP_STATE_FINISHED)
            return lh->errnum;
        if (lh->state == LOOKUP_STATE_CHECK_ROOT
            || lh->state == LOOKUP_STATE_WALK
            || lh->state == LOOKUP_STATE_VALUE)
            return EAGAIN;
    }
    return EINVAL;
}

int lookup_get_aux_errnum (lookup_t *lh)
{
    if (lh && lh->magic == LOOKUP_MAGIC)
        return lh->aux_errnum;
    return EINVAL;
}

int lookup_set_aux_errnum (lookup_t *lh, int errnum)
{
    if (lh && lh->magic == LOOKUP_MAGIC) {
        lh->aux_errnum = errnum;
        return lh->aux_errnum;
    }
    return EINVAL;
}

json_t *lookup_get_value (lookup_t *lh)
{
    if (lh
        && lh->magic == LOOKUP_MAGIC
        && lh->state == LOOKUP_STATE_FINISHED
        && lh->errnum == 0)
        return json_incref (lh->val);
    return NULL;
}

int lookup_iter_missing_refs (lookup_t *lh, lookup_ref_f cb, void *data)
{
    if (lh
        && lh->magic == LOOKUP_MAGIC
        && (lh->state == LOOKUP_STATE_CHECK_ROOT
            || lh->state == LOOKUP_STATE_WALK
            || lh->state == LOOKUP_STATE_VALUE)) {
        if (lh->valref_missing_refs) {
            int refcount, i;

            if (!treeobj_is_valref (lh->valref_missing_refs)) {
                errno = ENOTRECOVERABLE;
                return -1;
            }

            refcount = treeobj_get_count (lh->valref_missing_refs);
            assert (refcount > 0);

            for (i = 0; i < refcount; i++) {
                struct cache_entry *hp;
                const char *ref;

                if (!(ref = treeobj_get_blobref (lh->valref_missing_refs, i)))
                    return -1;

                if (!(hp = cache_lookup (lh->cache, ref, lh->current_epoch))
                    || !cache_entry_get_valid (hp)) {

                    /* valref points to raw data, raw_data flag is always
                     * true */
                    if (cb (lh, ref, data) < 0)
                        return -1;
                }
            }
        }
        else {
            if (cb (lh, lh->missing_ref, data) < 0)
                return -1;
        }
        return 0;
    }
    errno = EINVAL;
    return -1;
}

struct cache *lookup_get_cache (lookup_t *lh)
{
    if (lh && lh->magic == LOOKUP_MAGIC)
        return lh->cache;
    return NULL;
}

int lookup_get_current_epoch (lookup_t *lh)
{
    if (lh && lh->magic == LOOKUP_MAGIC)
        return lh->current_epoch;
    return -1;
}

const char *lookup_get_root_dir (lookup_t *lh)
{
    if (lh && lh->magic == LOOKUP_MAGIC)
        return lh->root_dir;
    return NULL;
}

const char *lookup_get_root_ref (lookup_t *lh)
{
    if (lh && lh->magic == LOOKUP_MAGIC)
        return lh->root_ref;
    return NULL;
}

const char *lookup_get_path (lookup_t *lh)
{
    if (lh && lh->magic == LOOKUP_MAGIC)
        return lh->path;
    return NULL;
}

int lookup_get_flags (lookup_t *lh)
{
    if (lh && lh->magic == LOOKUP_MAGIC)
        return lh->flags;
    return -1;
}

void *lookup_get_aux_data (lookup_t *lh)
{
    if (lh && lh->magic == LOOKUP_MAGIC)
        return lh->aux;
    return NULL;
}

int lookup_set_current_epoch (lookup_t *lh, int epoch)
{
    if (lh && lh->magic == LOOKUP_MAGIC) {
        lh->current_epoch = epoch;
        return 0;
    }
    return -1;
}

int lookup_set_aux_data (lookup_t *lh, void *data)
{
    if (lh && lh->magic == LOOKUP_MAGIC) {
        lh->aux = data;
        return 0;
    }
    return -1;
}

/* return 0 on success, -1 on failure.  On success, stall should be
 * checked */
static int get_single_blobref_valref_value (lookup_t *lh, bool *stall)
{
    struct cache_entry *hp;
    const char *reftmp;
    const void *valdata;
    int len;

    if (!(reftmp = treeobj_get_blobref (lh->wdirent, 0))) {
        lh->errnum = errno;
        return -1;
    }
    if (!(hp = cache_lookup (lh->cache, reftmp, lh->current_epoch))
        || !cache_entry_get_valid (hp)) {
        lh->valref_missing_refs = lh->wdirent;
        (*stall) = true;
        return 0;
    }
    if (cache_entry_get_raw (hp, &valdata, &len) < 0) {
        flux_log (lh->h, LOG_ERR, "cache_entry_get_raw");
        lh->errnum = ENOTRECOVERABLE;
        return -1;
    }
    if (!(lh->val = treeobj_create_val (valdata, len))) {
        lh->errnum = errno;
        return -1;
    }
    (*stall) = false;
    return 0;
}

static int get_multi_blobref_valref_length (lookup_t *lh, int refcount,
                                            int *total_len, bool *stall)
{
    struct cache_entry *hp;
    const char *reftmp;
    int total = 0;
    int len;
    int i;

    for (i = 0; i < refcount; i++) {
        if (!(reftmp = treeobj_get_blobref (lh->wdirent, i))) {
            lh->errnum = errno;
            return -1;
        }
        if (!(hp = cache_lookup (lh->cache, reftmp, lh->current_epoch))
            || !cache_entry_get_valid (hp)) {
            lh->valref_missing_refs = lh->wdirent;
            (*stall) = true;
            return 0;
        }

        if (cache_entry_get_raw (hp, NULL, &len) < 0) {
            flux_log (lh->h, LOG_ERR, "cache_entry_get_raw");
            lh->errnum = ENOTRECOVERABLE;
            return -1;
        }

        /* cache ensures all lens >= 0 */
        if (len > (INT_MAX - total)) {
            lh->errnum = EOVERFLOW;
            return -1;
        }
        total += len;
    }

    (*total_len) = total;
    (*stall) = false;
    return 0;
}

static char *get_multi_blobref_valref_data (lookup_t *lh, int refcount,
                                            int total_len)
{
    struct cache_entry *hp;
    const char *reftmp;
    const void *valdata;
    int len;
    char *valbuf = NULL;
    int pos = 0;
    int i;

    if (!(valbuf = malloc (total_len))) {
        lh->errnum = errno;
        return NULL;
    }

    for (i = 0; i < refcount; i++) {
        int ret;

        /* this function should only be called if all cache entries
         * known to be valid & raw, thus assert checks below */

        reftmp = treeobj_get_blobref (lh->wdirent, i);
        assert (reftmp);

        hp = cache_lookup (lh->cache, reftmp, lh->current_epoch);
        assert (hp);
        assert (cache_entry_get_valid (hp));

        ret = cache_entry_get_raw (hp, &valdata, &len);
        assert (ret == 0);

        memcpy (valbuf + pos, valdata, len);
        pos += len;
        assert (pos <= total_len);
    }

    return valbuf;
}

/* return 0 on success, -1 on failure.  On success, stall should be
 * check */
static int get_multi_blobref_valref_value (lookup_t *lh, int refcount,
                                           bool *stall)
{
    char *valbuf = NULL;
    int total_len = 0;
    int rc = -1;

    if (get_multi_blobref_valref_length (lh, refcount, &total_len, stall) < 0)
        goto done;

    if ((*stall) == true) {
        rc = 0;
        goto done;
    }

    if (!(valbuf = get_multi_blobref_valref_data (lh, refcount, total_len)))
        goto done;

    if (!(lh->val = treeobj_create_val (valbuf, total_len))) {
        lh->errnum = errno;
        goto done;
    }

    (*stall) = false;
    rc = 0;
done:
    free (valbuf);
    return rc;
}

bool lookup (lookup_t *lh)
{
    const json_t *valtmp = NULL;
    const char *reftmp;
    struct cache_entry *hp;
    int refcount;

    if (!lh || lh->magic != LOOKUP_MAGIC) {
        errno = EINVAL;
        return true;
    }

    switch (lh->state) {
        case LOOKUP_STATE_INIT:
        case LOOKUP_STATE_CHECK_ROOT:
            /* special case root */
            if (!strcmp (lh->path, ".")) {
                if ((lh->flags & FLUX_KVS_TREEOBJ)) {
                    if (!(lh->val = treeobj_create_dirref (lh->root_dir))) {
                        lh->errnum = errno;
                        goto done;
                    }
                } else {
                    if (!(lh->flags & FLUX_KVS_READDIR)) {
                        lh->errnum = EISDIR;
                        goto done;
                    }
                    if (!(hp = cache_lookup (lh->cache,
                                             lh->root_ref,
                                             lh->current_epoch))
                        || !cache_entry_get_valid (hp)) {
                        lh->state = LOOKUP_STATE_CHECK_ROOT;
                        lh->missing_ref = lh->root_ref;
                        goto stall;
                    }
                    if (!(valtmp = cache_entry_get_treeobj (hp))) {
                        flux_log (lh->h, LOG_ERR,
                                  "root_ref points to non-treeobj");
                        lh->errnum = EINVAL;
                        goto done;
                    }
                    if (!treeobj_is_dir (valtmp)) {
                        /* root_ref points to not dir */
                        lh->errnum = ENOTRECOVERABLE;
                        goto done;
                    }
                    if (!(lh->val = treeobj_deep_copy (valtmp))) {
                        lh->errnum = errno;
                        goto done;
                    }
                }
                goto done;
            }

            lh->state = LOOKUP_STATE_WALK;
            /* fallthrough */
        case LOOKUP_STATE_WALK:
            if (!walk (lh))
                goto stall;
            if (lh->errnum != 0)
                goto done;
            if (!lh->wdirent) {
                //lh->errnum = ENOENT;
                goto done; /* a NULL response is not necessarily an error */
            }

            lh->state = LOOKUP_STATE_VALUE;
            /* fallthrough */
        case LOOKUP_STATE_VALUE:
            if ((lh->flags & FLUX_KVS_TREEOBJ)) {
                if (!(lh->val = treeobj_deep_copy (lh->wdirent)))
                    lh->errnum = errno;
                goto done;
            }

            if (treeobj_is_dirref (lh->wdirent)) {
                if ((lh->flags & FLUX_KVS_READLINK)) {
                    lh->errnum = EINVAL;
                    goto done;
                }
                if (!(lh->flags & FLUX_KVS_READDIR)) {
                    lh->errnum = EISDIR;
                    goto done;
                }
                if ((refcount = treeobj_get_count (lh->wdirent)) < 0) {
                    lh->errnum = errno;
                    goto done;
                }
                if (refcount != 1) {
                    flux_log (lh->h, LOG_ERR, "invalid dirref count: %d",
                              refcount);
                    lh->errnum = ENOTRECOVERABLE;
                    goto done;
                }
                if (!(reftmp = treeobj_get_blobref (lh->wdirent, 0))) {
                    lh->errnum = errno;
                    goto done;
                }
                if (!(hp = cache_lookup (lh->cache, reftmp, lh->current_epoch))
                    || !cache_entry_get_valid (hp)) {
                    lh->missing_ref = reftmp;
                    goto stall;
                }
                if (!(valtmp = cache_entry_get_treeobj (hp))) {
                    flux_log (lh->h, LOG_ERR, "dirref points to non-treeobj");
                    lh->errnum = ENOTRECOVERABLE;
                    goto done;
                }
                if (!treeobj_is_dir (valtmp)) {
                    /* dirref points to not dir */
                    lh->errnum = ENOTRECOVERABLE;
                    goto done;
                }
                if (!(lh->val = treeobj_deep_copy (valtmp))) {
                    lh->errnum = errno;
                    goto done;
                }
            } else if (treeobj_is_valref (lh->wdirent)) {
                bool stall;

                if ((lh->flags & FLUX_KVS_READLINK)) {
                    lh->errnum = EINVAL;
                    goto done;
                }
                if ((lh->flags & FLUX_KVS_READDIR)) {
                    lh->errnum = ENOTDIR;
                    goto done;
                }
                if ((refcount = treeobj_get_count (lh->wdirent)) < 0) {
                    lh->errnum = errno;
                    goto done;
                }
                if (!refcount) {
                    flux_log (lh->h, LOG_ERR, "invalid valref count: %d",
                              refcount);
                    lh->errnum = ENOTRECOVERABLE;
                    goto done;
                }
                if (refcount == 1) {
                    if (get_single_blobref_valref_value (lh, &stall) < 0)
                        goto done;
                    if (stall)
                        goto stall;
                }
                else {
                    if (get_multi_blobref_valref_value (lh,
                                                        refcount,
                                                        &stall) < 0)
                        goto done;
                    if (stall)
                        goto stall;
                }
            } else if (treeobj_is_dir (lh->wdirent)) {
                if ((lh->flags & FLUX_KVS_READLINK)) {
                    lh->errnum = EINVAL;
                    goto done;
                }
                if (!(lh->flags & FLUX_KVS_READDIR)) {
                    lh->errnum = EISDIR;
                    goto done;
                }
                if (!(lh->val = treeobj_deep_copy (lh->wdirent))) {
                    lh->errnum = errno;
                    goto done;
                }
            } else if (treeobj_is_val (lh->wdirent)) {
                if ((lh->flags & FLUX_KVS_READLINK)) {
                    lh->errnum = EINVAL;
                    goto done;
                }
                if ((lh->flags & FLUX_KVS_READDIR)) {
                    lh->errnum = ENOTDIR;
                    goto done;
                }
                if (!(lh->val = treeobj_deep_copy (lh->wdirent))) {
                    lh->errnum = errno;
                    goto done;
                }
            } else if (treeobj_is_symlink (lh->wdirent)) {
                /* this should be "impossible" */
                if (!(lh->flags & FLUX_KVS_READLINK)) {
                    lh->errnum = EPROTO;
                    goto done;
                }
                if (lh->flags & FLUX_KVS_READDIR) {
                    lh->errnum = ENOTDIR;
                    goto done;
                }
                if (!(lh->val = treeobj_deep_copy (lh->wdirent))) {
                    lh->errnum = errno;
                    goto done;
                }
            } else {
                char *s = json_dumps (lh->wdirent, 0);
                flux_log (lh->h, LOG_ERR, "%s: corrupt dirent: %s",
                          __FUNCTION__, s);
                free (s);
                lh->errnum = ENOTRECOVERABLE;
                goto done;
            }
            /* val now contains the requested object (copied) */
            break;
        case LOOKUP_STATE_FINISHED:
            break;
        default:
            flux_log (lh->h, LOG_ERR, "%s: invalid state %d",
                      __FUNCTION__, lh->state);
            lh->errnum = ENOTRECOVERABLE;
            goto done;
    }

done:
    lh->state = LOOKUP_STATE_FINISHED;
    return true;
stall:
    return false;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
