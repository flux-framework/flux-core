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

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"

#include "cache.h"
#include "proto.h"
#include "json_dirent.h"
#include "json_util.h"

#include "lookup.h"

/* Break cycles in symlink references.
 */
#define SYMLINK_CYCLE_LIMIT 10

typedef struct {
    int depth;
    char *path_copy;            /* for internal parsing, do not use */
    json_object *dirent;
    zlist_t *pathcomps;
} walk_level_t;

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
                                        json_object *root_dirent,
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
    const char *ref;
    const char *link;
    json_object *dir;
    walk_level_t *wl = NULL;
    char *pathcomp;

    wl = zlist_head (lh->levels);

    /* walk directories */
    while ((pathcomp = zlist_head (wl->pathcomps))) {

        /* Check for errors in dirent before looking up reference.
         * Note that reference to lookup is determined in final
         * error check.
         */

        if (json_object_object_get_ex (wl->dirent, "DIRVAL", NULL)) {
            /* N.B. in current code, directories are never stored
             * by value */
            log_msg_exit ("%s: unexpected DIRVAL: "
                          "lh->path=%s pathcomp=%s: wl->dirent=%s ",
                          __FUNCTION__, lh->path, pathcomp,
                          Jtostr (wl->dirent));
        } else if ((Jget_str (wl->dirent, "FILEREF", NULL)
                    || json_object_object_get_ex (wl->dirent,
                                                  "FILEVAL",
                                                  NULL))) {
            /* don't return ENOENT or ENOTDIR, error to be
             * determined by caller */
            goto error;
        } else if (!Jget_str (wl->dirent, "DIRREF", &ref)) {
            log_msg_exit ("%s: unknown dirent type: "
                          "lh->path=%s pathcomp=%s: wl->dirent=%s ",
                          __FUNCTION__, lh->path, pathcomp,
                          Jtostr (wl->dirent));
        }

        /* Get directory reference of path component */

        if (!(dir = cache_lookup_and_get_json (lh->cache,
                                               ref,
                                               lh->current_epoch))) {
            lh->missing_ref = ref;
            goto stall;
        }

        if (!json_object_object_get_ex (dir, pathcomp, &wl->dirent))
            /* not necessarily ENOENT, let caller decide */
            goto error;

        /* Resolve dirent if it is a link */

        if (Jget_str (wl->dirent, "LINKVAL", &link)) {

            /* Follow link if in middle of path or if end of path,
             * flags say we can follow it
             */
            if (!last_pathcomp (wl->pathcomps, pathcomp)
                || (!(lh->flags & KVS_PROTO_READLINK)
                    && !(lh->flags & KVS_PROTO_TREEOBJ))) {

                if (wl->depth == SYMLINK_CYCLE_LIMIT) {
                    lh->errnum = ELOOP;
                    goto error;
                }

                /* "recursively" determine link dirent */
                if (!(wl = walk_levels_push (lh,
                                             link,
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

    lh->val = NULL;
    lh->missing_ref = NULL;
    lh->errnum = 0;

    lh->root_dirent = dirent_create ("DIRREF", lh->root_ref);

    if (!(lh->levels = zlist_new ())) {
        errno = ENOMEM;
        goto cleanup;
    }

    /* first depth is level 0 */
    if (!walk_levels_push (lh, lh->path, 0))
        goto cleanup;

    lh->wdirent = NULL;

    return lh;

 cleanup:
    lookup_destroy (lh);
    return NULL;
}

void lookup_destroy (lookup_t *lh)
{
    if (lh) {
        free (lh->root_dir);
        free (lh->root_ref_copy);
        free (lh->path);
        Jput (lh->root_dirent);
        zlist_destroy (&lh->levels);
        free (lh);
    }
}

bool lookup (lookup_t *lh)
{
    json_object *vp, *valtmp = NULL;
    const char *reftmp;

    if (!lh) {
        errno = EINVAL;
        return true;
    }

    /* special case root */
    if (!strcmp (lh->path, ".")) {
        if ((lh->flags & KVS_PROTO_TREEOBJ)) {
            lh->val = dirent_create ("DIRREF", (char *)lh->root_dir);
        } else {
            if (!(lh->flags & KVS_PROTO_READDIR)) {
                lh->errnum = EISDIR;
                goto done;
            }
            if (!(valtmp = cache_lookup_and_get_json (lh->cache,
                                                      lh->root_ref,
                                                      lh->current_epoch))) {
                lh->missing_ref = lh->root_ref;
                goto stall;
            }
            lh->val = json_object_get (valtmp);
        }
        goto done;
    } else {
        if (!walk (lh))
            goto stall;
        if (lh->errnum != 0)
            goto done;
        if (!lh->wdirent) {
            //lh->errnum = ENOENT;
            goto done; /* a NULL response is not necessarily an error */
        }

        if ((lh->flags & KVS_PROTO_TREEOBJ)) {
            lh->val = json_object_get (lh->wdirent);
            goto done;
        }

        if (json_object_object_get_ex (lh->wdirent, "DIRREF", &vp)) {
            if ((lh->flags & KVS_PROTO_READLINK)) {
                lh->errnum = EINVAL;
                goto done;
            }
            if (!(lh->flags & KVS_PROTO_READDIR)) {
                lh->errnum = EISDIR;
                goto done;
            }
            reftmp = json_object_get_string (vp);
            if (!(valtmp = cache_lookup_and_get_json (lh->cache,
                                                      reftmp,
                                                      lh->current_epoch))) {
                lh->missing_ref = reftmp;
                goto stall;
            }
            lh->val = json_object_copydir (valtmp);
        } else if (json_object_object_get_ex (lh->wdirent, "FILEREF", &vp)) {
            if ((lh->flags & KVS_PROTO_READLINK)) {
                lh->errnum = EINVAL;
                goto done;
            }
            if ((lh->flags & KVS_PROTO_READDIR)) {
                lh->errnum = ENOTDIR;
                goto done;
            }
            reftmp = json_object_get_string (vp);
            if (!(valtmp = cache_lookup_and_get_json (lh->cache,
                                                      reftmp,
                                                      lh->current_epoch))) {
                lh->missing_ref = reftmp;
                goto stall;
            }
            lh->val = json_object_get (valtmp);
        } else if (json_object_object_get_ex (lh->wdirent, "DIRVAL", &vp)) {
            if ((lh->flags & KVS_PROTO_READLINK)) {
                lh->errnum = EINVAL;
                goto done;
            }
            if (!(lh->flags & KVS_PROTO_READDIR)) {
                lh->errnum = EISDIR;
                goto done;
            }
            lh->val = json_object_copydir (vp);
        } else if (json_object_object_get_ex (lh->wdirent, "FILEVAL", &vp)) {
            if ((lh->flags & KVS_PROTO_READLINK)) {
                lh->errnum = EINVAL;
                goto done;
            }
            if ((lh->flags & KVS_PROTO_READDIR)) {
                lh->errnum = ENOTDIR;
                goto done;
            }
            lh->val = json_object_get (vp);
        } else if (json_object_object_get_ex (lh->wdirent, "LINKVAL", &vp)) {
            if (!(lh->flags & KVS_PROTO_READLINK)
                || (lh->flags & KVS_PROTO_READDIR)) {
                lh->errnum = EPROTO;
                goto done;
            }
            lh->val = json_object_get (vp);
        } else
            log_msg_exit ("%s: corrupt dirent: %s", __FUNCTION__,
                          Jtostr (lh->wdirent));
        /* val now contains the requested object (copied) */
    }
done:
    return true;
stall:
    return false;
}
