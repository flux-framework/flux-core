/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_RESOURCE_RUTIL_H
#define _FLUX_RESOURCE_RUTIL_H

/* Clear/set all the ids from 'ids2' in 'ids1'.
 * If ids2 == NULL, do nothing.
 * Return 0 on success, -1 on failure with errno set.
 */
int rutil_idset_sub (struct idset *ids1, const struct idset *ids2);
int rutil_idset_add (struct idset *ids1, const struct idset *ids2);

/* Compare 'old_set' to 'new_set'.
 * Create '*add' for ids in new_set but not in old_set (sets NULL if n/a).
 * Create '*sub' for ids in old_set but not in new_set (sets NULL if n/a).
 */
int rutil_idset_diff (const struct idset *old_set,
                      const struct idset *new_set,
                      struct idset **add,
                      struct idset **sub);

/* Compute an idset that combines all the valid ranks in a resource object.
 */
struct idset *rutil_idset_from_resobj (const json_t *resobj);

/* Clear any ranks from resource object keys that are present in 'ids'.
 */
json_t *rutil_resobj_sub (const json_t *resobj, const struct idset *ids);

/* Check whether id is a member of encoded idset
 */
bool rutil_idset_decode_test (const char *idset, unsigned long id);

/* Set key=val in a json object, where val is the string
 * representation of 'ids', or the empty string if 'ids' is NULL.
 */
int rutil_set_json_idset (json_t *o,
                          const char *key,
                          const struct idset *ids);

/* Return true if requests have the same sender.
 * Messages can be NULL or have no sender (returns false).
 */
bool rutil_match_request_sender (const flux_msg_t *msg1,
                                 const flux_msg_t *msg2);


#endif /* !_FLUX_RESOURCE_RUTIL_H */


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
