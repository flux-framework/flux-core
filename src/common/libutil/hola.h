/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_HOLA_H
#define _UTIL_HOLA_H

#include "src/common/libczmqcontainers/czmq_containers.h"

/* Lists are not internally created/destroyed automatically by default.
 * hola_hash_add() / hola_hash_delete() must be called to create/destroy them.
 * These flags allow automatic behavior to be enabled at list creation.
 */
enum {
    HOLA_AUTOCREATE = 1,    // create list on first addition
    HOLA_AUTODESTROY = 2,   // destroy list on last removal
};

struct hola *hola_create (int flags);
void hola_destroy (struct hola *hola);

void hola_set_list_destructor (struct hola *hola, zlistx_destructor_fn fun);
void hola_set_list_duplicator (struct hola *hola, zlistx_duplicator_fn fun);

void hola_set_hash_key_destructor (struct hola *hola, zhashx_destructor_fn fun);
void hola_set_hash_key_duplicator (struct hola *hola, zhashx_duplicator_fn fun);
void hola_set_hash_key_comparator (struct hola *hola, zhashx_comparator_fn fun);
void hola_set_hash_key_hasher (struct hola *hola, zhashx_hash_fn fun);

const void *hola_hash_first (struct hola *hola);
const void *hola_hash_next (struct hola *hola);
int hola_hash_add (struct hola *hola, const void *key);
int hola_hash_delete (struct hola *hola, const void *key);
size_t hola_hash_size (struct hola *hola);
zlistx_t *hola_hash_lookup (struct hola *hola, const void *key);

// returns list item
void *hola_list_first (struct hola *hola, const void *key);
void *hola_list_next (struct hola *hola, const void *key);

// returns list handle
void *hola_list_add_end (struct hola *hola, const void *key, void *item);
void *hola_list_cursor (struct hola *hola, const void *key);

int hola_list_delete (struct hola *hola, const void *key, void *handle);
size_t hola_list_size (struct hola *hola, const void *key);


#endif /* !_UTIL_HOLA_H */

// vi:ts=4 sw=4 expandtab
