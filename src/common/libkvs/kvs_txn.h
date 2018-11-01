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

#ifndef _FLUX_CORE_KVS_TXN_H
#define _FLUX_CORE_KVS_TXN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

typedef struct flux_kvs_txn flux_kvs_txn_t;

flux_kvs_txn_t *flux_kvs_txn_create (void);
void flux_kvs_txn_destroy (flux_kvs_txn_t *txn);

int flux_kvs_txn_put (flux_kvs_txn_t *txn, int flags,
                      const char *key, const char *value);

/* N.B. splitting at 80 columns confuses python cffi parser */
int flux_kvs_txn_vpack (flux_kvs_txn_t *txn, int flags, const char *key, const char *fmt, va_list ap);

int flux_kvs_txn_pack (flux_kvs_txn_t *txn, int flags, const char *key,
                       const char *fmt, ...);

int flux_kvs_txn_put_raw (flux_kvs_txn_t *txn, int flags,
                          const char *key, const void *data, int len);

int flux_kvs_txn_put_treeobj (flux_kvs_txn_t *txn, int flags,
                              const char *key, const char *treeobj);

int flux_kvs_txn_mkdir (flux_kvs_txn_t *txn, int flags,
                        const char *key);

int flux_kvs_txn_unlink (flux_kvs_txn_t *txn, int flags,
                         const char *key);

int flux_kvs_txn_symlink (flux_kvs_txn_t *txn, int flags,
                          const char *key, const char *target);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_TXN_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
