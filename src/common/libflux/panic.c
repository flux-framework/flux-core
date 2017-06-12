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
#include <syslog.h>
#include <unistd.h>
#include <signal.h>

#include "panic.h"
#include "flog.h"
#include "rpc.h"

#include "src/common/libutil/log.h"


int flux_panic (flux_t *h, int rank, const char *msg)
{
    uint32_t nodeid = rank < 0 ? FLUX_NODEID_ANY : rank;
    flux_future_t *f = NULL;
    int rc = -1;

    f = flux_rpcf (h, "cmb.panic", nodeid, FLUX_RPC_NORESPONSE,
                   "{s:s}", "msg", msg ? msg : "");
    if (!f)
        goto done;
    /* No reply */
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

void flux_assfail (flux_t *h, char *ass, char *file, int line)
{
    flux_log (h, LOG_CRIT, "assertion failure: %s:%d: %s", file, line, ass);
    sleep (5);
    if (raise (SIGABRT) < 0)
        exit (1);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
