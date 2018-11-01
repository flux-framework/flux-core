/*****************************************************************************\
 *  Copyright (c) 2017 Lawrence Livermore National Security, LLC.  Produced at
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

#ifndef _FLUX_CORE_KVS_LOOKUP_H
#define _FLUX_CORE_KVS_LOOKUP_H

#ifdef __cplusplus
extern "C" {
#endif

flux_future_t *flux_kvs_lookup (flux_t *h, int flags, const char *key);
flux_future_t *flux_kvs_lookupat (flux_t *h, int flags, const char *key,
                                  const char *treeobj);

int flux_kvs_lookup_get (flux_future_t *f, const char **value);
int flux_kvs_lookup_get_unpack (flux_future_t *f, const char *fmt, ...);
int flux_kvs_lookup_get_raw (flux_future_t *f, const void **data, int *len);
int flux_kvs_lookup_get_treeobj (flux_future_t *f, const char **treeobj);
int flux_kvs_lookup_get_dir (flux_future_t *f, const flux_kvsdir_t **dir);
int flux_kvs_lookup_get_symlink (flux_future_t *f, const char **target);

const char *flux_kvs_lookup_get_key (flux_future_t *f);

/* Cancel a FLUX_KVS_WATCH "stream".
 * Once the cancel request is processed, an ENODATA error response is sent,
 * thus the user should continue to reset and consume responses until an
 * error occurs, after which it is safe to destroy the future.
 */
int flux_kvs_lookup_cancel (flux_future_t *f);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_LOOKUP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
