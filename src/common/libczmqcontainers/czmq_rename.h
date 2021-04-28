/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _CZMQ_RENAME_H
#define _CZMQ_RENAME_H

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

#define zlistx_t fzlistx_t
#define zlistx_destructor_fn fzlistx_destructor_fn
#define zlistx_duplicator_fn fzlistx_duplicator_fn
#define zlistx_comparator_fn fzlistx_comparator_fn
#define zlistx_new fzlistx_new
#define zlistx_destroy fzlistx_destroy
#define zlistx_add_start fzlistx_add_start
#define zlistx_add_end fzlistx_add_end
#define zlistx_size fzlistx_size
#define zlistx_head fzlistx_head
#define zlistx_tail fzlistx_tail
#define zlistx_first fzlistx_first
#define zlistx_next fzlistx_next
#define zlistx_prev fzlistx_prev
#define zlistx_last fzlistx_last
#define zlistx_item fzlistx_item
#define zlistx_cursor fzlistx_cursor
#define zlistx_handle_item fzlistx_handle_item
#define zlistx_find fzlistx_find
#define zlistx_detach fzlistx_detach
#define zlistx_detach_cur fzlistx_detach_cur
#define zlistx_delete fzlistx_delete
#define zlistx_move_start fzlistx_move_start
#define zlistx_move_end fzlistx_move_end
#define zlistx_purge fzlistx_purge
#define zlistx_sort fzlistx_sort
#define zlistx_insert fzlistx_insert
#define zlistx_reorder fzlistx_reorder
#define zlistx_dup fzlistx_dup
#define zlistx_set_destructor fzlistx_set_destructor
#define zlistx_set_duplicator fzlistx_set_duplicator
#define zlistx_set_comparator fzlistx_set_comparator
#define zlistx_test fzlistx_test
#define zlistx_unpack fzlistx_unpack
#define zlistx_pack fzlistx_pack

#define zhash_t fzhash_t
#define zhash_free_fn fzhash_free_fn
#define zhash_new fzhash_new
#define zhash_unpack fzhash_unpack
#define zhash_destroy fzhash_destroy
#define zhash_insert fzhash_insert
#define zhash_update fzhash_update
#define zhash_delete fzhash_delete
#define zhash_lookup fzhash_lookup
#define zhash_rename fzhash_rename
#define zhash_freefn fzhash_freefn
#define zhash_size fzhash_size
#define zhash_dup fzhash_dup
#define zhash_keys fzhash_keys
#define zhash_first fzhash_first
#define zhash_next fzhash_next
#define zhash_cursor fzhash_cursor
#define zhash_comment fzhash_comment
#define zhash_pack fzhash_pack
#define zhash_save fzhash_save
#define zhash_load fzhash_load
#define zhash_refresh fzhash_refresh
#define zhash_autofree fzhash_autofree
#define zhash_test fzhash_test

#define zlist_t fzlist_t
#define zlist_compare_fn fzlist_compare_fn
#define zlist_free_fn fzlist_free_fn
#define zlist_new fzlist_new
#define zlist_destroy fzlist_destroy
#define zlist_first fzlist_first
#define zlist_next fzlist_next
#define zlist_last fzlist_last
#define zlist_head fzlist_head
#define zlist_tail fzlist_tail
#define zlist_item fzlist_item
#define zlist_append fzlist_append
#define zlist_push fzlist_push
#define zlist_pop fzlist_pop
#define zlist_exists fzlist_exists
#define zlist_remove fzlist_remove
#define zlist_dup fzlist_dup
#define zlist_purge fzlist_purge
#define zlist_size fzlist_size
#define zlist_sort fzlist_sort
#define zlist_autofree fzlist_autofree
#define zlist_comparefn fzlist_comparefn
#define zlist_freefn fzlist_freefn
#define zlist_test fzlist_test

#ifdef __cplusplus
}
#endif

#endif
