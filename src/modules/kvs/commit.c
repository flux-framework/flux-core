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

commit_t *commit_create (fence_t *f, void *aux)
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
    c->aux = aux;
    c->state = COMMIT_STATE_INIT;
    return c;
error:
    commit_destroy (c);
    return NULL;
}

void commit_destroy (commit_t *c)
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

commit_mgr_t *commit_mgr_create (void *aux)
{
    commit_mgr_t *cm;

    if (!(cm = calloc (1, sizeof (*cm)))) {
        errno = ENOMEM;
        goto error;
    }
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
