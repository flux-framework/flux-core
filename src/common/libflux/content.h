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

#ifndef _FLUX_CORE_CONTENT_H
#define _FLUX_CORE_CONTENT_H

#include <flux/core.h>

#ifdef __cplusplus
extern "C" {
#endif

/* flags */
enum {
    CONTENT_FLAG_CACHE_BYPASS = 1,/* request direct to backing store */
    CONTENT_FLAG_UPSTREAM = 2,    /* make request of upstream TBON peer */
};

/* Send request to load blob by blobref.
 */
flux_future_t *flux_content_load (flux_t *h,
                                  const char *blobref, int flags);

/* Get result of load request (blob).
 * This blocks until response is received.
 * Storage for 'buf' belongs to 'f' and is valid until 'f' is destroyed.
 * Returns 0 on success, -1 on failure with errno set.
 */
int flux_content_load_get (flux_future_t *f, const void **buf, int *len);

/* Send request to store blob.
 */
flux_future_t *flux_content_store (flux_t *h,
                                   const void *buf, int len, int flags);

/* Get result of store request (blobref).
 * Storage for 'blobref' belongs to 'f' and is valid until 'f' is destroyed.
 * Returns 0 on success, -1 on failure with errno set.
 */
int flux_content_store_get (flux_future_t *f, const char **blobref);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_CONTENT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
