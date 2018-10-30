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

#ifndef _FLUX_CORE_MSG_HANDLER_H
#define _FLUX_CORE_MSG_HANDLER_H

#include "message.h"
#include "handle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct flux_msg_handler flux_msg_handler_t;

typedef void (*flux_msg_handler_f)(flux_t *h, flux_msg_handler_t *mh,
                                   const flux_msg_t *msg, void *arg);

flux_msg_handler_t *flux_msg_handler_create (flux_t *h,
                                             const struct flux_match match,
                                             flux_msg_handler_f cb, void *arg);

void flux_msg_handler_destroy (flux_msg_handler_t *mh);

void flux_msg_handler_start (flux_msg_handler_t *mh);
void flux_msg_handler_stop (flux_msg_handler_t *mh);

/* By default, only messages from FLUX_ROLE_OWNER are delivered to handler.
 * Use _allow_rolemask() add roles, _deny_rolemask() to remove them.
 * (N.B. FLUX_ROLE_OWNER cannot be denied)
 */
void flux_msg_handler_allow_rolemask (flux_msg_handler_t *mh,
                                      uint32_t rolemask);
void flux_msg_handler_deny_rolemask (flux_msg_handler_t *mh,
                                     uint32_t rolemask);

struct flux_msg_handler_spec {
    int typemask;
    const char *topic_glob;
    flux_msg_handler_f cb;
    uint32_t rolemask;
};
#define FLUX_MSGHANDLER_TABLE_END { 0, NULL, NULL, 0 }

int flux_msg_handler_addvec (flux_t *h,
                             const struct flux_msg_handler_spec tab[],
                             void *arg,
                             flux_msg_handler_t **msg_handlers[]);
void flux_msg_handler_delvec (flux_msg_handler_t *msg_handlers[]);

/* Requeue any unmatched messages, if handle was cloned.
 */
int flux_dispatch_requeue (flux_t *h);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_MSG_HANDLER_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
