/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* An idset is an internally sorted set of non-negative integers  (0,1,2,3...).
 */


#ifndef FLUX_IDSET_H
#define FLUX_IDSET_H

#include <sys/types.h>
#include <sys/param.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum idset_flags {
    IDSET_FLAG_AUTOGROW = 1, // allow idset universe size to automatically grow
    IDSET_FLAG_BRACKETS = 2, // encode non-singleton idset with brackets
    IDSET_FLAG_RANGE = 4,    // encode with ranges ("2,3,4,8" -> "2-4,8")
    IDSET_FLAG_INITFULL = 8, // initilize/grow idset with all ids set
};

#define IDSET_INVALID_ID    (UINT_MAX - 1)

/* Create/destroy an idset.
 * Set the initial universe size to 'size' (0 means implementation uses a
 * default size).
 * If 'flags' includes IDSET_FLAG_AUTOGROW, the idset is resized to fit if
 * an id >= size is set.
 * Returns idset on success, or NULL on failure with errno set.
 */
struct idset *idset_create (size_t size, int flags);
void idset_destroy (struct idset *idset);

/* Get the current universe size of the idset.
 * A set with a universe size of N holds ids from 0 to N - 1.
 */
size_t idset_universe_size (const struct idset *idset);

/* Make an exact duplicate of idset.
 * Returns copy on success, NULL on failure with errno set.
 */
struct idset *idset_copy (const struct idset *idset);

/* Encode 'idset' to a string, which the caller must free.
 * 'flags' may include IDSET_FLAG_BRACKETS, IDSET_FLAG_RANGE.
 * Returns string on success, or NULL on failure with errno set.
 */
char *idset_encode (const struct idset *idset, int flags);

/* Decode string 's' to an idset.
 * Returns idset on success, or NULL on failure with errno set.
 */
struct idset *idset_decode (const char *s);

/* Decode 'len' chars of string 's' to an idset.
 * Returns idset on success, or NULL on failure with errno set.
 */
struct idset *idset_ndecode (const char *s, size_t len);

/* Add id (or range [lo-hi]) to idset.
 * Return 0 on success, -1 on failure with errno set.
 */
int idset_set (struct idset *idset, unsigned int id);
int idset_range_set (struct idset *idset, unsigned int lo, unsigned int hi);

/* Remove id (or range [lo-hi]) from idset.
 * It is not a failure if id is not a member of idset.
 * Return 0 on success, -1 on failure with errno set.
 */
int idset_clear (struct idset *idset, unsigned int id);
int idset_range_clear (struct idset *idset, unsigned int lo, unsigned int hi);

/* Return true if id is a member of idset.
 * If id is not a member, or idset or id is invalid, return false.
 */
bool idset_test (const struct idset *idset, unsigned int id);

/* Return the first id in the idset.
 * Returns IDSET_INVALID_ID if the idset is empty.
 */
unsigned int idset_first (const struct idset *idset);

/* Return the next id after 'prev' in the idset.
 * Returns IDSET_INVALID_ID if prev is the last id.
 */
unsigned int idset_next (const struct idset *idset, unsigned int prev);

/* Returns the last id in the idset.
 * Returns IDSET_INVALID_ID if the idset is empty.
 */
unsigned int idset_last (const struct idset *idset);

/* Return the number of id's in idset.
 * If idset is invalid, return 0.
 */
size_t idset_count (const struct idset *idset);

/* Return true if the two idsets set1, set2 are equal, i.e. they both
 * have the same integers set.
 */
bool idset_equal (const struct idset *a, const struct idset *);

/* Return a new set = a union b on success,
 * NULL on failure with errno set.
 */
struct idset *idset_union (const struct idset *a, const struct idset *b);

/* Perform a += b.
 * Return 0 on success, -1 on failure with errno set.
 */
int idset_add (struct idset *a, const struct idset *b);

/* Return a new set = a - b on success,
 * NULL on failure with errno set.
 */
struct idset *idset_difference (const struct idset *a, const struct idset *b);

/* Perform a -= b.
 * Return 0 on success, -1 on failure with errno set.
 */
int idset_subtract (struct idset *a, const struct idset *b);

/* Remove all elements from set x.
 */
#define idset_clear_all(x)  idset_subtract ((x), (x))

/* Return a new set = a intersect b on success,
 * NULL on failure with errno set.
 */
struct idset *idset_intersect (const struct idset *a, const struct idset *b);

/* Return true if set a intersects set b.
 */
bool idset_has_intersection (const struct idset *a, const struct idset *b);

/* Convenience interfaces for using idset as an "integer allocator".
 * The set must be created with IDSET_FLAG_INITFULL.
 * idset_free() doesn't check for double frees, but idset_free_check() does.
 * It returns -1 and sets errno to EEXIST when that happens.
 */
int idset_alloc (struct idset *idset, unsigned int *val);
void idset_free (struct idset *idset, unsigned int val);
int idset_free_check (struct idset *idset, unsigned int val);

#ifdef __cplusplus
}
#endif

#endif /* !FLUX_IDSET_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
