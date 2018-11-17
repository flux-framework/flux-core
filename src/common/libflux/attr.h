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

#ifndef _FLUX_CORE_ATTR_H
#define _FLUX_CORE_ATTR_H

/* broker attributes
 *
 * Brokers have configuration attributes.
 * Values are local to a particular broker rank.
 * Some may be overridden on the broker command line with -Sattr=val.
 * The following commands are available for manipulating attributes
 * on the running system:
 *   flux lsattr [-v]
 *   flux setattr name value
 *   flux getattr name
 * In additon, the following functions may be used to get/set broker
 * attributes programmatically.
 */

#include "handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Get the value for attribute 'name' from the local broker.
 * Returns value on success, NULL on failure with errno set.
 * This function performs a synchronous RPC to the broker if the
 * attribute is not found in cache, thus may block for the round-trip
 * communication.
 */
const char *flux_attr_get (flux_t *h, const char *name);

/* Set the value for attribute 'name' from the local broker.
 * Returns value on success, NULL on failure with errno set.
 * This function performs a synchronous RPC to the broker,
 * thus blocks for the round-trip communication.
 */
int flux_attr_set (flux_t *h, const char *name, const char *val);


/* hotwire flux_attr_get()'s cache for testing */
int flux_attr_set_cacheonly (flux_t *h, const char *name, const char *val);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_ATTR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
