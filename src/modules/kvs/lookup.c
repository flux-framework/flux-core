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
    json_t *dirent;
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
    const char *missing_ref;    /* on stall, missing ref to load */
    int errnum;                 /* errnum if error */

    /* API internal */
    json_t *root_dirent;
    zlist_t *levels;
    json_t *wdirent;       /* result after walk() */
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
    json_t *dir;
    walk_level_t *wl = NULL;
    char *pathcomp;

    wl = zlist_head (lh->levels);

    /* walk directories */
    while ((pathcomp = zlist_head (wl->pathcomps))) {

        /* Get directory of dirent */

        if (treeobj_is_dirref (wl->dirent)) {
            const char *refstr;
            int refcount;

            if ((refcount = treeobj_get_count (wl->dirent)) < 0) {
                lh->errnum = errno;
                goto error;
            }

            if (refcount != 1) {
                flux_log (lh->h, LOG_ERR, "invalid dirref count: %d", refcount);
                lh->errnum = EPERM;
                goto error;
            }

            if (!(refstr = treeobj_get_blobref (wl->dirent, 0))) {
                lh->errnum = errno;
                goto error;
            }

            if (!(dir = cache_lookup_and_get_json (lh->cache,
                                                   refstr,
                                                   lh->current_epoch))) {
                lh->missing_ref = refstr;
                goto stall;
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
                lh->errnum = EPERM;
                goto error;
            }
        }

        /* Get directory reference of path component from directory */

        if (!(wl->dirent = treeobj_get_entry (dir, pathcomp))) {
            /* if entry does not exist, not necessarily ENOENT error,
             * let caller decide.  If error not ENOENT, return to
             * caller. */
            if (errno != ENOENT)
                lh->errnum = errno;
            goto error;
        }

        /* Resolve dirent if it is a link */

        if (treeobj_is_symlink (wl->dirent)) {
            json_t *link;
            const char *linkstr;

            if (!(link = treeobj_get_data (wl->dirent))) {
                lh->errnum = errno;
                goto error;
            }

            linkstr = json_string_value (link);

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

json_t *lookup_get_value (lookup_t *lh)
{
    if (lh
        && lh->magic == LOOKUP_MAGIC
        && lh->state == LOOKUP_STATE_FINISHED
        && lh->errnum == 0)
        return json_incref (lh->val);
    return NULL;
}

const char *lookup_get_missing_ref (lookup_t *lh)
{
    if (lh
        && lh->magic == LOOKUP_MAGIC
        && (lh->state == LOOKUP_STATE_CHECK_ROOT
            || lh->state == LOOKUP_STATE_WALK
            || lh->state == LOOKUP_STATE_VALUE))
        return lh->missing_ref;
    return NULL;
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

int lookup_set_aux_data (lookup_t *lh, void *data) {
    if (lh && lh->magic == LOOKUP_MAGIC) {
        lh->aux = data;
        return 0;
    }
    return -1;
}

bool lookup (lookup_t *lh)
{
    json_t *valtmp = NULL;
    const char *reftmp;
    const char *strtmp;
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
                    valtmp = cache_lookup_and_get_json (lh->cache,
                                                        lh->root_ref,
                                                        lh->current_epoch);
                    if (!valtmp) {
                        lh->state = LOOKUP_STATE_CHECK_ROOT;
                        lh->missing_ref = lh->root_ref;
                        goto stall;
                    }
                    lh->val = json_incref (valtmp);
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
                lh->val = json_incref (lh->wdirent);
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
                    lh->errnum = EPERM;
                    goto done;
                }
                if (!(reftmp = treeobj_get_blobref (lh->wdirent, 0))) {
                    lh->errnum = errno;
                    goto done;
                }
                valtmp = cache_lookup_and_get_json (lh->cache,
                                                    reftmp,
                                                    lh->current_epoch);
                if (!valtmp) {
                    lh->missing_ref = reftmp;
                    goto stall;
                }
                if (!treeobj_is_dir (valtmp)) {
                    /* dirref points to not dir */
                    lh->errnum = EPERM;
                    goto done;
                }
                lh->val = json_incref (valtmp);
            } else if (treeobj_is_valref (lh->wdirent)) {
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
                if (refcount != 1) {
                    flux_log (lh->h, LOG_ERR, "invalid valref count: %d",
                              refcount);
                    lh->errnum = EPERM;
                    goto done;
                }
                if (!(reftmp = treeobj_get_blobref (lh->wdirent, 0))) {
                    lh->errnum = errno;
                    goto done;
                }
                valtmp = cache_lookup_and_get_json (lh->cache,
                                                    reftmp,
                                                    lh->current_epoch);
                if (!valtmp) {
                    lh->missing_ref = reftmp;
                    goto stall;
                }
                if (!json_is_string (valtmp)) {
                    /* valref points to non-string */
                    lh->errnum = EPERM;
                    goto done;
                }

                /* Place base64 opaque data into treeobj val object */
                strtmp = json_string_value (valtmp);
                if (!(lh->val = treeobj_create_val_base64 (strtmp))) {
                    lh->errnum = errno;
                    goto done;
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
                lh->val = json_incref (lh->wdirent);
            } else if (treeobj_is_val (lh->wdirent)) {
                if ((lh->flags & FLUX_KVS_READLINK)) {
                    lh->errnum = EINVAL;
                    goto done;
                }
                if ((lh->flags & FLUX_KVS_READDIR)) {
                    lh->errnum = ENOTDIR;
                    goto done;
                }
                lh->val = json_incref (lh->wdirent);
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
                lh->val = json_incref (lh->wdirent);
            } else {
                char *s = json_dumps (lh->wdirent, 0);
                flux_log (lh->h, LOG_ERR, "%s: corrupt dirent: %s",
                          __FUNCTION__, s);
                free (s);
                lh->errnum = EPERM;
                goto done;
            }
            /* val now contains the requested object (copied) */
            break;
        case LOOKUP_STATE_FINISHED:
            break;
        default:
            flux_log (lh->h, LOG_ERR, "%s: invalid state %d",
                      __FUNCTION__, lh->state);
            lh->errnum = EPERM;
            goto done;
    }

done:
    lh->state = LOOKUP_STATE_FINISHED;
    return true;
stall:
    return false;
}
