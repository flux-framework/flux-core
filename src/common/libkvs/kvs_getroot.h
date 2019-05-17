/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_KVS_GETROOT_H
#    define _FLUX_CORE_KVS_GETROOT_H

#    ifdef __cplusplus
extern "C" {
#    endif

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

#    ifdef __cplusplus
}
#    endif

#endif /* !_FLUX_CORE_KVS_GETROOT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
