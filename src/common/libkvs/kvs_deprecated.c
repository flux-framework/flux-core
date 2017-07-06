/*****************************************************************************\
 *  Copyright (c) 2017 Lawrence Livermore National Security, LLC.  Produced at
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
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <czmq.h>
#include <flux/core.h>

#include "kvs_deprecated.h"

static int common_put_obj (flux_t *h, const char *key, json_object *val)
{
    int rc = -1;
    const char *json_str = NULL;

    if (val)
        json_str = json_object_to_json_string (val);
    if (kvs_put (h, key, json_str) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

int kvsdir_put_obj (kvsdir_t *dir, const char *name, json_object *val)
{
    flux_t *h = kvsdir_handle (dir);
    char *key = kvsdir_key_at (dir, name);
    int rc = -1;

    /* N.B. dropped EROFS check for dir->rootdir != NULL */

    rc = common_put_obj (h, key, val);
    free (key);
    return rc;
}

/* Watch
 */

struct watch_state {
    kvs_set_obj_f cb;
    void *arg;
};

static int watch_cb (const char *key, const char *val, void *arg, int errnum)
{
    struct watch_state *ws = arg;
    json_object *obj = NULL;
    int rc;

    if (errnum == 0 && val != NULL) {
        if (!(obj = json_tokener_parse (val)))
            errnum = ENOMEM;
    }
    rc = ws->cb (key, obj, arg, errnum);
    if (obj)
        json_object_put (obj);
    return rc;
}

int kvs_watch_obj (flux_t *h, const char *key, kvs_set_obj_f set, void *arg)
{
    struct watch_state *ws = calloc (1, sizeof (*ws));
    if (!ws) {
        errno = ENOMEM;
        return -1;
    }
    ws->cb = set;
    ws->arg = arg;

    if (kvs_watch (h, key, watch_cb, ws) < 0) {
        free (ws);
        return -1;
    }

    /* Free the little wrapper struct when handle is destroyed
     */
    char auxkey[64];
    snprintf (auxkey, sizeof (auxkey), "flux::kvs_watch_%p", ws);
    flux_aux_set (h, auxkey, ws, free);

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
