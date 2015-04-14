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

#include "peer.h"

void peer_destroy (peer_t *p)
{
    free (p);
}

peer_t *peer_create (void)
{
    peer_t *p = xzmalloc (sizeof (*p));
    return p;
}

peer_t *peer_add (zhash_t *zh, const char *uuid)
{
    peer_t *p = peer_create ();
    zhash_update (zh, uuid, p);
    zhash_freefn (zh, uuid, (zhash_free_fn *)peer_destroy);
    return p;
}

peer_t *peer_lookup (zhash_t *zh, const char *uuid)
{
    peer_t *p = zhash_lookup (zh, uuid);
    return p;
}

void peer_set_lastseen (peer_t *p, int val)
{
    p->lastseen = val;
}

int peer_get_lastseen (peer_t *p)
{
    return p->lastseen;
}

void peer_set_modflag (peer_t *p, bool val)
{
    p->modflag = val;
}

bool peer_get_modflag (peer_t *p)
{
    return p->modflag;
}

void peer_set_mute (peer_t *p, bool val)
{
    p->mute = val;
}

bool peer_get_mute (peer_t *p)
{
    return p->mute;
}

void peer_checkin (zhash_t *zh, const char *uuid, int now)
{
    peer_t *p = peer_lookup (zh, uuid);
    if (!p)
        p = peer_add (zh, uuid);
    peer_set_lastseen (p, now);
}

int peer_idle (zhash_t *zh, const char *uuid, int now)
{
    peer_t *p = peer_lookup (zh, uuid);
    if (!p)
        return now;
    return now = peer_get_lastseen (p);
}

void peer_mute (zhash_t *zh, const char *uuid)
{
    peer_t *p = peer_lookup (zh, uuid);
    if (!p)
        p = peer_add (zh, uuid);
    peer_set_mute (p, true);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
