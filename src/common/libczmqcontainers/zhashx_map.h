/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _ZHASHX_MAP_H
#define _ZHASHX_MAP_H

#ifdef __cplusplus
extern "C" {
#endif

#define zhashx_t fzhashx_t
#define zhashx_destructor_fn fzhashx_destructor_fn
#define zhashx_duplicator_fn fzhashx_duplicator_fn
#define zhashx_comparator_fn fzhashx_comparator_fn
#define zhashx_free_fn fzhashx_free_fn
#define zhashx_hash_fn fzhashx_hash_fn
#define zhashx_serializer_fn fzhashx_serializer_fn
#define zhashx_deserializer_fn fzhashx_deserializer_fn
#define zhashx_new fzhashx_new
#define zhashx_unpack fzhashx_unpack
#define zhashx_destroy fzhashx_destroy
#define zhashx_insert fzhashx_insert
#define zhashx_update fzhashx_update
#define zhashx_delete fzhashx_delete
#define zhashx_purge fzhashx_purge
#define zhashx_lookup fzhashx_lookup
#define zhashx_rename fzhashx_rename
#define zhashx_freefn fzhashx_freefn
#define zhashx_size fzhashx_size
#define zhashx_keys fzhashx_keys
#define zhashx_values fzhashx_values
#define zhashx_first fzhashx_first
#define zhashx_next fzhashx_next
#define zhashx_cursor fzhashx_cursor
#define zhashx_comment fzhashx_comment
#define zhashx_save fzhashx_save
#define zhashx_load fzhashx_load
#define zhashx_refresh fzhashx_refresh
#define zhashx_pack fzhashx_pack
#define zhashx_dup fzhashx_dup
#define zhashx_set_destructor fzhashx_set_destructor
#define zhashx_set_duplicator fzhashx_set_duplicator
#define zhashx_set_key_destructor fzhashx_set_key_destructor
#define zhashx_set_key_duplicator fzhashx_set_key_duplicator
#define zhashx_set_key_comparator fzhashx_set_key_comparator
#define zhashx_set_key_hasher fzhashx_set_key_hasher
#define zhashx_dup_v2 fzhashx_dup_v2
#define zhashx_test fzhashx_test
#define zhashx_unpack_own fzhashx_unpack_own
#define zhashx_pack_own fzhashx_pack_own

#ifdef __cplusplus
}
#endif

#endif
