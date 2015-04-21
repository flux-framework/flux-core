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

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"

#include "heartbeat.h"
#include "peer.h"

peerhash_t *peerhash_create (void)
{
    peerhash_t *ph = xzmalloc (sizeof *ph);
    if (!(ph->zh = zhash_new ()))
        oom ();
    return ph;
}

void peerhash_destroy (peerhash_t *ph)
{
    if (ph) {
        zhash_destroy (&ph->zh);
        free (ph);
    }
}

zlist_t *peerhash_keys (peerhash_t *ph)
{
    return zhash_keys (ph->zh);
}

void peerhash_set_heartbeat (peerhash_t *ph, heartbeat_t *hb)
{
    ph->hb = hb;
}

static void peer_destroy (peer_t *p)
{
    free (p);
}

static peer_t *peer_create (void)
{
    peer_t *p = xzmalloc (sizeof (*p));
    return p;
}

peer_t *peer_add (peerhash_t *ph, const char *uuid)
{
    peer_t *p = peer_create ();
    zhash_update (ph->zh, uuid, p);
    zhash_freefn (ph->zh, uuid, (zhash_free_fn *)peer_destroy);
    return p;
}

void peer_del (peerhash_t *ph, const char *uuid)
{
    zhash_delete (ph->zh, uuid);
}

peer_t *peer_lookup (peerhash_t *ph, const char *uuid)
{
    peer_t *p = zhash_lookup (ph->zh, uuid);
    return p;
}

void peer_set_arg (peer_t *p, void *arg)
{
    p->arg = arg;
}

void *peer_get_arg (peer_t *p)
{
    return p->arg;
}

void peer_set_mute (peer_t *p, bool val)
{
    p->mute = val;
}

bool peer_get_mute (peer_t *p)
{
    return p->mute;
}

void peer_checkin (peerhash_t *ph, const char *uuid)
{
    int now = heartbeat_get_epoch (ph->hb);
    peer_t *p = peer_lookup (ph, uuid);
    if (!p)
        p = peer_add (ph, uuid);
    p->lastseen = now;
}

int peer_idle (peerhash_t *ph, const char *uuid)
{
    int now = heartbeat_get_epoch (ph->hb);
    peer_t *p = peer_lookup (ph, uuid);
    if (!p)
        return now;
    return now - p->lastseen;
}

void peer_mute (peerhash_t *ph, const char *uuid)
{
    peer_t *p = peer_lookup (ph, uuid);
    if (!p)
        p = peer_add (ph, uuid);
    peer_set_mute (p, true);
}

json_object *peer_list_encode (peerhash_t *ph)
{
    int now = heartbeat_get_epoch (ph->hb);
    JSON out = Jnew ();
    zlist_t *keys;
    char *key;
    peer_t *p;

    if (!(keys = peerhash_keys (ph)))
        oom ();
    key = zlist_first (keys);
    while (key) {
        if ((p = peer_lookup (ph, key))) {
            JSON o = Jnew ();
            Jadd_int (o, "idle", now - p->lastseen);
            Jadd_obj (out, key, o);
            Jput (o);
        }
        key = zlist_next (keys);
    }
    zlist_destroy (&keys);
    return out ;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
