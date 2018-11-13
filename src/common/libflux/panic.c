/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <syslog.h>
#include <unistd.h>
#include <signal.h>
#include <flux/core.h>

#include "panic.h"


int flux_panic (flux_t *h, uint32_t nodeid, int flags, const char *reason)
{
    flux_future_t *f;

    if (!h || !reason || flags != 0) {
        errno = EINVAL;
        return -1;
    }
    if (!(f = flux_rpc_pack (h, "cmb.panic", nodeid, FLUX_RPC_NORESPONSE,
                             "{s:s s:i}",
                             "reason", reason,
                             "flags", flags)))
        return -1;
    flux_future_destroy (f);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
