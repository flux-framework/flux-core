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

#ifndef _FLUX_CORE_KVS_H
#define _FLUX_CORE_KVS_H

#include <flux/core.h>

#include "kvs_dir.h"
#include "kvs_lookup.h"
#include "kvs_getroot.h"
#include "kvs_classic.h"
#include "kvs_watch.h"
#include "kvs_txn.h"
#include "kvs_commit.h"
#include "kvs_eventlog.h"
#include "kvs_copy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KVS_PRIMARY_NAMESPACE "primary"

enum kvs_op {
    FLUX_KVS_READDIR = 1,
    FLUX_KVS_READLINK = 2,
    FLUX_KVS_WATCH = 4,
    FLUX_KVS_WATCH_WAITCREATE = 8,
    FLUX_KVS_TREEOBJ = 16,
    FLUX_KVS_APPEND = 32,
};

typedef struct flux_kvs_namespace_itr flux_kvs_namespace_itr_t;

/* Namespace
 * - namespace create only creates the namespace on rank 0.  Other
 *   ranks initialize against that namespace the first time they use
 *   it.
 * - namespace remove marks the namespace for removal on all ranks.
 *   Garbage collection will happen in the background and the
 *   namespace will official be removed.  The removal is "eventually
 *   consistent".
 *
 *   NOTE: Take care to avoid conflicting with C++'s keyword "namespace"
 *   in the external interfaces.
 */
flux_future_t *flux_kvs_namespace_create (flux_t *h, const char *name_space,
                                          uint32_t owner, int flags);
flux_future_t *flux_kvs_namespace_remove (flux_t *h, const char *name_space);

/* Namespace list
 * - Returns flux_kvs_namespace_itr_t for iterating through namespaces.
 *
 *   NOTE: Take care to avoid conflicting with C++'s keyword "namespace"
 *   in the external interfaces.
 */
flux_kvs_namespace_itr_t *flux_kvs_namespace_list (flux_t *h);
const char *flux_kvs_namespace_itr_next (flux_kvs_namespace_itr_t *itr,
                                         uint32_t *owner,
                                         int *flags);
void flux_kvs_namespace_itr_rewind (flux_kvs_namespace_itr_t *itr);
void flux_kvs_namespace_itr_destroy (flux_kvs_namespace_itr_t *itr);

/* Namespace Selection
 * - configure a KVS namespace to use in all kvs operations using this
 *   handle.
 * - if never set, the value from the environment variable
 *   FLUX_KVS_NAMESPACE is used.
 * - if FLUX_KVS_NAMESPACE is not set, KVS_PRIMARY_NAMESPACE is assumed.
 *
 *   NOTE: Take care to avoid conflicting with C++'s keyword "namespace"
 *   in the external interfaces.
 */
int flux_kvs_set_namespace (flux_t *h, const char *name_space);
const char *flux_kvs_get_namespace (flux_t *h);

/* Synchronization:
 * Process A commits data, then gets the store version V and sends it to B.
 * Process B waits for the store version to be >= V, then reads data.
 * To use an alternate namespace, set environment variable FLUX_KVS_NAMESPACE.
 */
int flux_kvs_get_version (flux_t *h, int *versionp);
int flux_kvs_wait_version (flux_t *h, int version);

/* Garbage collect the cache.  On the root node, drop all data that
 * doesn't have a reference in the namespace.  On other nodes, the entire
 * cache is dropped and will be reloaded on demand.
 * Returns -1 on error (errno set), 0 on success.
 */
int flux_kvs_dropcache (flux_t *h);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
