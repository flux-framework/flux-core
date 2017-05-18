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

/* Break cycles in symlink references.
 */
#define SYMLINK_CYCLE_LIMIT 10

typedef struct {
    int depth;
    char *path_copy;            /* for internal parsing, do not use */
    json_object *dirent;
    json_object *dir;
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

static walk_level_t *walk_level_create (const char *path, json_object *root,
                                        int depth)
{
    walk_level_t *wl = xzmalloc (sizeof (*wl));

    wl->path_copy = xstrdup (path);
    wl->depth = depth;
    wl->dirent = NULL;
    wl->dir = root;
    if (!(wl->pathcomps = walk_pathcomps_zlist_create (wl)))
        goto error;

    return wl;

 error:
    walk_level_destroy (wl);
    return NULL;
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
static bool walk (struct cache *cache, int current_epoch, json_object *root,
                  const char *path, int flags, int depth,
                  json_object **direntp, const char **missing_ref, int *ep)
{
    const char *ref;
    const char *link;
    walk_level_t *wl = NULL;
    char *pathcomp;
    int errnum = 0;

    if (!(wl = walk_level_create (path, root, depth))) {
        errnum = errno;
        goto error;
    }

    /* walk directories */
    while ((pathcomp = zlist_head (wl->pathcomps))) {

        if (!json_object_object_get_ex (wl->dir, pathcomp, &wl->dirent))
            /* not necessarily ENOENT, let caller decide */
            goto error;

        if (Jget_str (wl->dirent, "LINKVAL", &link)) {

            /* Follow link if in middle of path or if end of path,
             * flags say we can follow it
             */
            if (!last_pathcomp (wl->pathcomps, pathcomp)
                || (!(flags & KVS_PROTO_READLINK)
                    && !(flags & KVS_PROTO_TREEOBJ))) {

                if (depth == SYMLINK_CYCLE_LIMIT) {
                    errnum = ELOOP;
                    goto error;
                }

                if (!walk (cache, current_epoch, root, link, flags,
                           depth + 1, &wl->dirent, missing_ref, ep))
                    goto stall;
                if (*ep != 0) {
                    errnum = *ep;
                    goto error;
                }
                if (!wl->dirent)
                    /* not necessarily ENOENT, let caller decide */
                    goto error;
            }
        }

        if (!last_pathcomp (wl->pathcomps, pathcomp)) {
            /* Check for errors in dirent before looking up reference.
             * Note that reference to lookup is determined in final
             * error check.
             */

            if (json_object_object_get_ex (wl->dirent, "DIRVAL", NULL)) {
                /* N.B. in current code, directories are never stored
                 * by value */
                log_msg_exit ("%s: unexpected DIRVAL: "
                              "path=%s pathcomp=%s: wl->dirent=%s ",
                              __FUNCTION__, path, pathcomp,
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
                              "path=%s pathcomp=%s: wl->dirent=%s ",
                              __FUNCTION__, path, pathcomp,
                              Jtostr (wl->dirent));
            }

            if (!(wl->dir = cache_lookup_and_get_json (cache,
                                                       ref,
                                                       current_epoch))) {
                *missing_ref = ref;
                goto stall;
            }
        }

        zlist_remove (wl->pathcomps, pathcomp);
    }
    *direntp = wl->dirent;
    walk_level_destroy (wl);
    return true;
error:
    if (errnum != 0)
        *ep = errnum;
    *direntp = NULL;
    walk_level_destroy (wl);
    return true;
stall:
    walk_level_destroy (wl);
    return false;
}

bool lookup (struct cache *cache, int current_epoch, json_object *root,
             const char *rootdir, const char *path, int flags, 
             json_object **valp, const char **missing_ref, int *ep)
{
    json_object *vp, *dirent, *val = NULL;
    int walk_errnum = 0;
    int errnum = 0;

    if (!strcmp (path, ".")) { /* special case root */
        if ((flags & KVS_PROTO_TREEOBJ)) {
            val = dirent_create ("DIRREF", (char *)rootdir);
        } else {
            if (!(flags & KVS_PROTO_READDIR)) {
                errnum = EISDIR;
                goto done;
            }
            val = json_object_get (root);
        }
    } else {
        if (!walk (cache, current_epoch, root, path, flags, 0,
                   &dirent, missing_ref, &walk_errnum))
            goto stall;
        if (walk_errnum != 0) {
            errnum = walk_errnum;
            goto done;
        }
        if (!dirent) {
            //errnum = ENOENT;
            goto done; /* a NULL response is not necessarily an error */
        }
        if ((flags & KVS_PROTO_TREEOBJ)) {
            val = json_object_get (dirent);
            goto done;
        }
        if (json_object_object_get_ex (dirent, "DIRREF", &vp)) {
            if ((flags & KVS_PROTO_READLINK)) {
                errnum = EINVAL;
                goto done;
            }
            if (!(flags & KVS_PROTO_READDIR)) {
                errnum = EISDIR;
                goto done;
            }
            if (!(val = cache_lookup_and_get_json (cache,
                                                   json_object_get_string (vp),
                                                   current_epoch))) {
                *missing_ref = json_object_get_string (vp);
                goto stall;
            }
            val = json_object_copydir (val);
        } else if (json_object_object_get_ex (dirent, "FILEREF", &vp)) {
            if ((flags & KVS_PROTO_READLINK)) {
                errnum = EINVAL;
                goto done;
            }
            if ((flags & KVS_PROTO_READDIR)) {
                errnum = ENOTDIR;
                goto done;
            }
            if (!(val = cache_lookup_and_get_json (cache,
                                                   json_object_get_string (vp),
                                                   current_epoch))) {
                *missing_ref = json_object_get_string (vp);
                goto stall;
            }
            val = json_object_get (val);
        } else if (json_object_object_get_ex (dirent, "DIRVAL", &vp)) {
            if ((flags & KVS_PROTO_READLINK)) {
                errnum = EINVAL;
                goto done;
            }
            if (!(flags & KVS_PROTO_READDIR)) {
                errnum = EISDIR;
                goto done;
            }
            val = json_object_copydir (vp);
        } else if (json_object_object_get_ex (dirent, "FILEVAL", &vp)) {
            if ((flags & KVS_PROTO_READLINK)) {
                errnum = EINVAL;
                goto done;
            }
            if ((flags & KVS_PROTO_READDIR)) {
                errnum = ENOTDIR;
                goto done;
            }
            val = json_object_get (vp);
        } else if (json_object_object_get_ex (dirent, "LINKVAL", &vp)) {
            if (!(flags & KVS_PROTO_READLINK) || (flags & KVS_PROTO_READDIR)) {
                errnum = EPROTO;
                goto done;
            }
            val = json_object_get (vp);
        } else
            log_msg_exit ("%s: corrupt dirent: %s", __FUNCTION__,
                          Jtostr (dirent));
    }
    /* val now contains the requested object (copied) */
done:
    *valp = val;
    if (errnum != 0)
        *ep = errnum;
    return true;
stall:
    return false;
}
