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
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"

#include "service.h"

struct svc_struct {
    svc_cb_f cb;
    void *cb_arg;
};

struct svchash_struct {
    zhash_t *zh;
};

svchash_t svchash_create (void)
{
    svchash_t sh = xzmalloc (sizeof *sh);
    if (!(sh->zh = zhash_new ()))
        oom ();
    return sh;
}

void svchash_destroy (svchash_t sh)
{
    if (sh) {
        zhash_destroy (&sh->zh);
        free (sh);
    }
}

static void svc_destroy (svc_t svc)
{
    free (svc);
}

static svc_t svc_create (void)
{
    svc_t svc = xzmalloc (sizeof (*svc));
    return svc;
}

void svc_remove (svchash_t sh, const char *name)
{
    zhash_delete (sh->zh, name);
}

svc_t svc_add (svchash_t sh, const char *name, svc_cb_f cb, void *arg)
{
    svc_t svc = svc_create ();
    svc->cb = cb;
    svc->cb_arg = arg;
    if (zhash_insert (sh->zh, name, svc) < 0) {
        svc_destroy (svc);
        errno = EEXIST;
        return NULL;
    }
    zhash_freefn (sh->zh, name, (zhash_free_fn *)svc_destroy);
    return svc;
}

static svc_t svc_lookup (svchash_t sh, const char *name)
{
    svc_t svc = zhash_lookup (sh->zh, name);
    if (!svc && strchr (name, '.')) {
        char *cpy = xstrdup (name);
        char *p = strchr (cpy, '.');
        if (p) {
            *p = '\0';
            svc = zhash_lookup (sh->zh, cpy);
        }
        free (cpy);
    }
    return svc;
}

int svc_sendmsg (svchash_t sh, zmsg_t **zmsg)
{
    char *topic = NULL;
    int type;
    svc_t svc;
    int rc = -1;

    if (flux_msg_get_type (*zmsg, &type) < 0)
        goto done;
    if (flux_msg_get_topic (*zmsg, &topic) < 0)
        goto done;
    if (!(svc = svc_lookup (sh, topic)) || !svc->cb) {
        errno = ENOSYS;
        goto done;
    }
    rc = svc->cb (zmsg, svc->cb_arg);
done:
    if (topic)
        free (topic);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
