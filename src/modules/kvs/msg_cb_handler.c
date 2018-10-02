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

#include "msg_cb_handler.h"

struct msg_cb_handler {
    flux_t *h;
    flux_msg_handler_t *mh;
    flux_msg_t *msg;
    void *arg;
    flux_msg_handler_f cb;
};

msg_cb_handler_t *msg_cb_handler_create (flux_t *h, flux_msg_handler_t *mh,
                                         const flux_msg_t *msg, void *arg,
                                         flux_msg_handler_f cb)
{
    msg_cb_handler_t *mcb;

    mcb = calloc (1, sizeof (*mcb));
    if (mcb) {
        mcb->h = h;
        mcb->mh = mh;
        mcb->arg = arg;
        mcb->cb = cb;
        if (msg && !(mcb->msg = flux_msg_copy (msg, true))) {
            msg_cb_handler_destroy (mcb);
            errno = ENOMEM;
            return NULL;
        }
    }
    return mcb;
}

void msg_cb_handler_destroy (msg_cb_handler_t *mcb)
{
    if (mcb) {
        if (mcb->msg)
            flux_msg_destroy (mcb->msg);
        free (mcb);
    }
}

int msg_cb_handler_msg_aux_set (msg_cb_handler_t *mcb, const char *name,
                                void *aux, flux_free_f destroy)
{
    if (mcb && mcb->msg)
        return flux_msg_aux_set (mcb->msg, name, aux, destroy);
    return -1;
}

void *msg_cb_handler_msg_aux_get (msg_cb_handler_t *mcb, const char *name)
{
    if (mcb && mcb->msg)
        return flux_msg_aux_get (mcb->msg, name);
    return NULL;
}

void msg_cb_handler_call (msg_cb_handler_t *mcb)
{
    if (mcb && mcb->cb)
        mcb->cb (mcb->h, mcb->mh, mcb->msg, mcb->arg);
}

const flux_msg_t *msg_cb_handler_get_msgcopy (msg_cb_handler_t *mcb)
{
    if (mcb)
        return mcb->msg;
    return NULL;
}

void msg_cb_handler_set_cb (msg_cb_handler_t *mcb, flux_msg_handler_f cb)
{
    if (mcb)
        mcb->cb = cb;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

