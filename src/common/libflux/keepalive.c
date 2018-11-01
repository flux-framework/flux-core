/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
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
#include <flux/core.h>

#include "keepalive.h"

flux_msg_t *flux_keepalive_encode (int errnum, int status)
{
    flux_msg_t *msg;

    if (!(msg = flux_msg_create (FLUX_MSGTYPE_KEEPALIVE)))
        goto error;
    if (flux_msg_set_errnum (msg, errnum) < 0)
        goto error;
    if (flux_msg_set_status (msg, status) < 0)
        goto error;
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

int flux_keepalive_decode (const flux_msg_t *msg, int *ep, int *sp)
{
    int rc = -1;
    int errnum, status;

    if (flux_msg_get_errnum (msg, &errnum) < 0)
        goto done;
    if (flux_msg_get_status (msg, &status) < 0)
        goto done;
    if (ep)
        *ep = errnum;
    if (sp)
        *sp = status;
    rc = 0;
done:
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
