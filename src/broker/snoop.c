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
#include <errno.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"

#include "endpt.h"
#include "snoop.h"

struct snoop_struct {
    flux_sec_t sec;
    zctx_t *zctx;
    endpt_t *snoop;
};

snoop_t snoop_create (void)
{
    snoop_t sn = xzmalloc (sizeof (*sn));
    return sn;
}

void snoop_destroy (snoop_t sn)
{
    if (sn) {
        if (sn->snoop)
            endpt_destroy (sn->snoop);
        free (sn);
    }
}

void snoop_set_sec (snoop_t sn, flux_sec_t sec)
{
    sn->sec = sec;
}

void snoop_set_zctx (snoop_t sn, zctx_t *zctx)
{
    sn->zctx = zctx;
}

void snoop_set_uri (snoop_t sn, const char *fmt, ...)
{
    va_list ap;

    if (sn->snoop)
        endpt_destroy (sn->snoop);
    va_start (ap, fmt);
    sn->snoop = endpt_vcreate (fmt, ap);
    va_end (ap);
}

static int snoop_bind (snoop_t sn)
{
    int rc = -1;
    if (!(sn->snoop->zs = zsocket_new (sn->zctx, ZMQ_PUB)))
        goto done;
    if (flux_sec_ssockinit (sn->sec, sn->snoop->zs) < 0) {
        //msg ("flux_sec_ssockinit: %s", flux_sec_errstr (sn->sec));
        goto done;
    }
    if (zsocket_bind (sn->snoop->zs, "%s", sn->snoop->uri) < 0)
        goto done;
    if (strchr (sn->snoop->uri, '*')) { /* capture dynamically assigned port */
        free (sn->snoop->uri);
        sn->snoop->uri = zsocket_last_endpoint (sn->snoop->zs);
    }
    rc = 0;
done:
    return rc;
}

const char *snoop_get_uri (snoop_t sn)
{
    if ((!sn->snoop || !sn->snoop->zs) && snoop_bind (sn) < 0)
        return NULL;
    return sn->snoop->uri;
}

int snoop_sendmsg (snoop_t sn, zmsg_t *zmsg)
{
    int rc = -1;
    zmsg_t *cpy = NULL;

    if (!sn->snoop || !sn->snoop->zs)
        return 0;
    if (!(cpy = zmsg_dup (zmsg)))
        oom ();
    rc = zmsg_send (&cpy, sn->snoop->zs);
    zmsg_destroy (&cpy);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
