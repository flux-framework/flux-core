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

#ifndef _FLUX_CORE_KVS_GETROOT_H
#define _FLUX_CORE_KVS_GETROOT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Request the current KVS root hash for namespace 'ns'. */
flux_future_t *flux_kvs_getroot (flux_t *h, const char *ns, int flags);

/* Decode KVS root hash response.
 *
 * treeobj - get the hash as an RFC 11 "dirref" object.
 * blobref - get the raw hash as a n RFC 10 "blobref".
 * sequence - get the commit sequence number
 * owner - get the userid of the namespace owner
 */
int flux_kvs_getroot_get_treeobj (flux_future_t *f, const char **treeobj);
int flux_kvs_getroot_get_blobref (flux_future_t *f, const char **blobref);
int flux_kvs_getroot_get_sequence (flux_future_t *f, int *seq);
int flux_kvs_getroot_get_owner (flux_future_t *f, uint32_t *owner);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_GETROOT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
