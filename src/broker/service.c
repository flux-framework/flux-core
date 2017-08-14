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
#include "src/common/libutil/oom.h"

#include "service.h"

struct svc_struct {
    svc_cb_f cb;
    void *cb_arg;
    char *alias;
};

struct service_switch {
    zhash_t *services;
    zhash_t *aliases;
};

struct service_switch *service_switch_create (void)
{
    struct service_switch *sh = xzmalloc (sizeof *sh);
    sh->services = zhash_new ();
    sh->aliases = zhash_new ();
    if (!sh->services || !sh->aliases)
        oom ();
    return sh;
}

void service_switch_destroy (struct service_switch *sh)
{
    if (sh) {
        zhash_destroy (&sh->services);
        zhash_destroy (&sh->aliases);
        free (sh);
    }
}

static void svc_destroy (svc_t *svc)
{
    if (svc) {
        if (svc->alias)
            free (svc->alias);
        free (svc);
    }
}

static svc_t *svc_create (void)
{
    svc_t *svc = xzmalloc (sizeof (*svc));
    return svc;
}

void svc_remove (struct service_switch *sh, const char *name)
{
    svc_t *svc = zhash_lookup (sh->services, name);
    if (svc) {
        if (svc->alias)
            zhash_delete (sh->aliases, svc->alias);
        zhash_delete (sh->services, name);
    }
}

svc_t *svc_add (struct service_switch *sh, const char *name, const char *alias,
                svc_cb_f cb, void *arg)
{
    svc_t *svc;
    int rc;
    if (zhash_lookup (sh->services, name)
            || (alias && zhash_lookup (sh->aliases, alias))) {
        errno = EEXIST;
        return NULL;
    }
    svc = svc_create ();
    svc->cb = cb;
    svc->cb_arg = arg;
    rc = zhash_insert (sh->services, name, svc);
    assert (rc == 0);
    zhash_freefn (sh->services, name, (zhash_free_fn *)svc_destroy);
    if (alias) {
        svc->alias = xstrdup (alias);
        rc = zhash_insert (sh->aliases, alias, svc);
        assert (rc == 0);
    }
    return svc;
}

int svc_sendmsg (struct service_switch *sh, const flux_msg_t *msg)
{
    const char *topic;
    int type;
    svc_t *svc;
    int rc = -1;

    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    if (flux_msg_get_topic (msg, &topic) < 0)
        goto done;
    if (!(svc = zhash_lookup (sh->services, topic)))
        svc = zhash_lookup (sh->aliases, topic);
    if (!svc && strchr (topic, '.')) {
        char *p, *short_topic = xstrdup (topic);
        if ((p = strchr (short_topic, '.')))
            *p = '\0';
        if (!(svc = zhash_lookup (sh->services, short_topic)))
            svc = zhash_lookup (sh->aliases, short_topic);
        free (short_topic);
    }
    if (!svc) {
        errno = ENOSYS;
        goto done;
    }
    rc = svc->cb (msg, svc->cb_arg);
done:
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
