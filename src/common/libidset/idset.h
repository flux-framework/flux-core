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

enum idset_flags {
    IDSET_FLAG_AUTOGROW = 1, // allow idset size to automatically grow
    IDSET_FLAG_BRACKETS = 2, // encode non-singleton idset with brackets
    IDSET_FLAG_RANGE = 4,    // encode with ranges ("2,3,4,8" -> "2-4,8")
};

#define IDSET_INVALID_ID    (UINT_MAX - 1)

/* Create/destroy an idset.
 * Set the initial size to 'size' (0 means implementation uses a default size).
 * If 'flags' includes IDSET_FLAG_AUTOGROW, the idset is resized to fit if
 * an id >= size is set.
 * Returns idset on success, or NULL on failure with errno set.
 */
struct idset *idset_create (size_t size, int flags);
void idset_destroy (struct idset *idset);

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
bool idset_equal (const struct idset *set1, const struct idset *set2);

/* Expand bracketed idset string(s) in 's', calling 'fun()' for each
 * expanded string.  'fun()' should return 0 on success, or -1 on failure
 * with errno set.  A fun() failure causes idset_format_map () to immediately
 * return -1.  'fun()' may may halt iteration without triggering an error
 * by setting *stop = true.
 *
 * idset_format_map () returns the number of times the map function was called
 * (including the stopping one, if any), or -1 on failure with errno set.
 *
 * This function recursively expands multiple bracketed idset strings from
 * left to right, so for example, "r[0-1]n[0-1]" expands to "r0n0", "r0n1",
 * "r1n0", "r1n1".
 *
 */
typedef int (*idset_format_map_f)(const char *s, bool *stop, void *arg);

int idset_format_map (const char *s, idset_format_map_f fun, void *arg);


#endif /* !FLUX_IDSET_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
