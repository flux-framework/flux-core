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
#include <stdbool.h>
#include <czmq.h>

#include "src/common/libflux/handle.h"
#include "src/common/libflux/message.h"

#include "handle.h"

int flux_sendmsg (flux_t *h, flux_msg_t **msg)
{
    if (flux_send (h, *msg, 0) < 0)
        return -1;
    flux_msg_destroy (*msg);
    *msg = NULL;
    return 0;
}

flux_msg_t *flux_recvmsg (flux_t *h, bool nonblock)
{
    return flux_recv (h, FLUX_MATCH_ANY, nonblock ? FLUX_O_NONBLOCK : 0);
}

flux_msg_t *flux_recvmsg_match (flux_t *h, struct flux_match match,
                                bool nonblock)
{
    return flux_recv (h, match, nonblock ? FLUX_O_NONBLOCK : 0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
