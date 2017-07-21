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
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"
#include "src/common/libkvs/jansson_dirent.h"

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

    if (!(pathcomps = zlist_new ())) {
        errno = ENOMEM;
        return NULL;
    }

    current = wl->path_copy;
    while ((next = strchr (current, '.'))) {
        *next++ = '\0';

        if (zlist_append (pathcomps, current) < 0)
            oom ();

        current = next;
    }

    if (zlist_append (pathcomps, current) < 0)
        oom ();

    return pathcomps;
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
    walk_level_t *wl = xzmalloc (sizeof (*wl));

    wl->path_copy = xstrdup (path);
    wl->depth = depth;
    wl->dirent = root_dirent;
    if (!(wl->pathcomps = walk_pathcomps_zlist_create (wl)))
        goto error;

    return wl;

 error:
    walk_level_destroy (wl);
    return NULL;
}

static walk_level_t *walk_levels_push (lookup_t *lh,
                                       const char *path,
                                       int depth)
{
    walk_level_t *wl;

    if (!(wl = walk_level_create (path, lh->root_dirent, depth)))
        return NULL;

    if (zlist_push (lh->levels, wl) < 0)
        oom ();
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
    json_t *dir, *ref, *link;
    walk_level_t *wl = NULL;
    char *pathcomp;

    wl = zlist_head (lh->levels);

    /* walk directories */
    while ((pathcomp = zlist_head (wl->pathcomps))) {

        /* Get directory of dirent */

        if ((ref = json_object_get (wl->dirent, "DIRREF"))) {
            const char *refstr = json_string_value (ref);

            if (!(dir = cache_lookup_and_get_json (lh->cache,
                                                   refstr,
                                                   lh->current_epoch))) {
                lh->missing_ref = refstr;
                goto stall;
            }
        } else {
            /* Unexpected dirent type */
            if (json_object_get (wl->dirent, "FILEREF")
                || json_object_get (wl->dirent, "FILEVAL")) {
                /* don't return ENOENT or ENOTDIR, error to be
                 * determined by caller */
                goto error;
            }
            else {
                char *s = json_dumps (wl->dirent, 0);
                log_msg ("%s: unknown/unexpected dirent type: "
                         "lh->path=%s pathcomp=%s: wl->dirent=%s ",
                         __FUNCTION__, lh->path, pathcomp, s);
                free (s);
                lh->errnum = EPERM;
                goto error;
            }
        }

        /* Get directory reference of path component from directory */

        if (!(wl->dirent = json_object_get (dir, pathcomp)))
            /* not necessarily ENOENT, let caller decide */
            goto error;

        /* Resolve dirent if it is a link */

        if ((link = json_object_get (wl->dirent, "LINKVAL"))) {
            const char *linkstr = json_string_value (link);

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
                         int flags)
{
    lookup_t *lh = NULL;

    if (!cache || !root_dir || !path) {
        errno = EINVAL;
        return NULL;
    }

    lh = xzmalloc (sizeof (*lh));

    lh->magic = LOOKUP_MAGIC;
    lh->cache = cache;
    lh->current_epoch = current_epoch;
    /* must duplicate these strings, user may not keep pointer
     * alive */
    lh->root_dir = xstrdup (root_dir);
    if (root_ref) {
        lh->root_ref_copy = xstrdup (root_ref);
        lh->root_ref = lh->root_ref_copy;
    }
    else {
        lh->root_ref_copy = NULL;
        lh->root_ref = lh->root_dir;
    }
    lh->path = xstrdup (path);
    lh->flags = flags;

    lh->aux = NULL;

    lh->val = NULL;
    lh->missing_ref = NULL;
    lh->errnum = 0;

    lh->root_dirent = j_dirent_create ("DIRREF", lh->root_ref);

    if (!(lh->levels = zlist_new ())) {
        errno = ENOMEM;
        goto cleanup;
    }

    /* first depth is level 0 */
    if (!walk_levels_push (lh, lh->path, 0))
        goto cleanup;

    lh->wdirent = NULL;
    lh->state = LOOKUP_STATE_INIT;

    return lh;

 cleanup:
    lookup_destroy (lh);
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
    json_t *vp, *valtmp = NULL;
    const char *reftmp;

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
                    lh->val = j_dirent_create ("DIRREF", (char *)lh->root_dir);
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

            if ((vp = json_object_get (lh->wdirent, "DIRREF"))) {
                if ((lh->flags & FLUX_KVS_READLINK)) {
                    lh->errnum = EINVAL;
                    goto done;
                }
                if (!(lh->flags & FLUX_KVS_READDIR)) {
                    lh->errnum = EISDIR;
                    goto done;
                }
                reftmp = json_string_value (vp);
                valtmp = cache_lookup_and_get_json (lh->cache,
                                                    reftmp,
                                                    lh->current_epoch);
                if (!valtmp) {
                    lh->missing_ref = reftmp;
                    goto stall;
                }
                lh->val = kvs_util_json_copydir (valtmp);
            } else if ((vp = json_object_get (lh->wdirent, "FILEREF"))) {
                if ((lh->flags & FLUX_KVS_READLINK)) {
                    lh->errnum = EINVAL;
                    goto done;
                }
                if ((lh->flags & FLUX_KVS_READDIR)) {
                    lh->errnum = ENOTDIR;
                    goto done;
                }
                reftmp = json_string_value (vp);
                valtmp = cache_lookup_and_get_json (lh->cache,
                                                    reftmp,
                                                    lh->current_epoch);
                if (!valtmp) {
                    lh->missing_ref = reftmp;
                    goto stall;
                }
                lh->val = json_incref (valtmp);
            } else if ((vp = json_object_get (lh->wdirent, "DIRVAL"))) {
                if ((lh->flags & FLUX_KVS_READLINK)) {
                    lh->errnum = EINVAL;
                    goto done;
                }
                if (!(lh->flags & FLUX_KVS_READDIR)) {
                    lh->errnum = EISDIR;
                    goto done;
                }
                lh->val = kvs_util_json_copydir (vp);
            } else if ((vp = json_object_get (lh->wdirent, "FILEVAL"))) {
                if ((lh->flags & FLUX_KVS_READLINK)) {
                    lh->errnum = EINVAL;
                    goto done;
                }
                if ((lh->flags & FLUX_KVS_READDIR)) {
                    lh->errnum = ENOTDIR;
                    goto done;
                }
                lh->val = json_incref (vp);
            } else if ((vp = json_object_get (lh->wdirent, "LINKVAL"))) {
                /* this should be "impossible" */
                if (!(lh->flags & FLUX_KVS_READLINK)) {
                    lh->errnum = EPROTO;
                    goto done;
                }
                if (lh->flags & FLUX_KVS_READDIR) {
                    lh->errnum = ENOTDIR;
                    goto done;
                }
                lh->val = json_incref (vp);
            } else {
                char *s = json_dumps (lh->wdirent, 0);
                log_msg ("%s: corrupt dirent: %s", __FUNCTION__, s);
                free (s);
                lh->errnum = EPERM;
                goto done;
            }
            /* val now contains the requested object (copied) */
            break;
        case LOOKUP_STATE_FINISHED:
            break;
        default:
            log_msg_exit ("%s: invalid state %d", __FUNCTION__, lh->state);
    }

done:
    lh->state = LOOKUP_STATE_FINISHED;
    return true;
stall:
    return false;
}
