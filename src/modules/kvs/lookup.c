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
    char *cpy = xstrdup (path);
    char *next, *name = cpy;
    const char *ref;
    const char *link;
    json_object *dirent = NULL;
    json_object *dir = root;
    int errnum = 0;

    depth++;

    /* walk directories */
    while ((next = strchr (name, '.'))) {
        *next++ = '\0';
        if (!json_object_object_get_ex (dir, name, &dirent))
            /* not necessarily ENOENT, let caller decide */
            goto error;
        if (Jget_str (dirent, "LINKVAL", &link)) {
            if (depth == SYMLINK_CYCLE_LIMIT) {
                errnum = ELOOP;
                goto error;
            }
            if (!walk (cache, current_epoch, root, link, flags, depth,
                       &dirent, missing_ref, ep))
                goto stall;
            if (*ep != 0) {
                errnum = *ep;
                goto error;
            }
            if (!dirent)
                /* not necessarily ENOENT, let caller decide */
                goto error;
        }

        /* Check for errors in dirent before looking up reference.
         * Note that reference to lookup is determined in final
         * error check.
         */

        if (json_object_object_get_ex (dirent, "DIRVAL", NULL)) {
            /* N.B. in current code, directories are never stored by value */
            log_msg_exit ("%s: unexpected DIRVAL: path=%s name=%s: dirent=%s ",
                          __FUNCTION__, path, name, Jtostr (dirent));
        } else if ((Jget_str (dirent, "FILEREF", NULL)
                    || json_object_object_get_ex (dirent, "FILEVAL", NULL))) {
            /* don't return ENOENT or ENOTDIR, error to be determined
             * by caller */
            goto error;
        } else if (!Jget_str (dirent, "DIRREF", &ref)) {
            log_msg_exit ("%s: unknown dirent type: path=%s name=%s: dirent=%s ",
                          __FUNCTION__, path, name, Jtostr (dirent));
        }

        if (!(dir = cache_lookup_and_get_json (cache,
                                               ref,
                                               current_epoch))) {
            *missing_ref = ref;
            goto stall;
        }

        name = next;
    }
    /* now terminal path component */
    if (json_object_object_get_ex (dir, name, &dirent) &&
        Jget_str (dirent, "LINKVAL", &link)) {
        if (!(flags & KVS_PROTO_READLINK) && !(flags & KVS_PROTO_TREEOBJ)) {
            if (depth == SYMLINK_CYCLE_LIMIT) {
                errnum = ELOOP;
                goto error;
            }
            if (!walk (cache, current_epoch, root, link, flags, depth,
                       &dirent, missing_ref, ep))
                goto stall;
            if (*ep != 0) {
                errnum = *ep;
                goto error;
            }
        }
    }
    free (cpy);
    *direntp = dirent;
    return true;
error:
    if (errnum != 0)
        *ep = errnum;
    free (cpy);
    *direntp = NULL;
    return true;
stall:
    free (cpy);
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
