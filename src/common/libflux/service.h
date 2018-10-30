/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
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

#ifndef _FLUX_CORE_SERVICE_H
#define _FLUX_CORE_SERVICE_H

#include "handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 
 *  Register service `name` with the broker for this handle. On success
 *   request messages sent to "name.*" will be routed to this handle 
 *   until `flux_service_remove()` is called for `name`, or upon
 *   disconnect. 
 *
 *  On success, the returned future will be fulfilled with no error, o/w
 *   the future is fulfilled with errnum set appropriately:
 *
 *   EINVAL - Invalid service name
 *   EEXIST - Service already registered under this name
 *   ENOENT - Unable to lookup route to requesting sender
 * 
 */
flux_future_t *flux_service_register (flux_t *h, const char *name);

/*
 *  Unregister a previously registered service `name` for this handle.
 *
 *  On success, the returned future is fulfilled with no error, o/w
 *   the future is fulfilled with errnum set appropriately:
 *
 *  ENOENT - No such service registered as `name`
 *  EINVAL - Sender does not match current owner of service
 * 
 */  
flux_future_t *flux_service_unregister (flux_t *h, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_SERVICE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
