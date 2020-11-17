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


/* Load data by path:
 * - rutil_read_file() returns data as a NULL-terminated string.
 * - rutil_load_file() parses data as a JSON object and returns it.
 * - rutil_load_xml_dir() parses <rank>.xml files in path, and returns
 *   a JSON object with ranks as keys and XML strings as values.
 * On error put human readable error in errbuf and return NULL
 */
char *rutil_read_file (const char *path, char *errbuf, int errbufsize);
json_t *rutil_load_file (const char *path, char *errbuf, int errbufsize);
json_t *rutil_load_xml_dir (const char *path, char *errbuf, int errbufsize);

/* Build object with idset keys.
 * Start with empty json_t object, then insert objects by id.
 * If json_equal() returns true on values, they are combined.
 */
int rutil_idkey_insert_id (json_t *obj, unsigned int id, json_t *val);
int rutil_idkey_insert_idset (json_t *obj, struct idset *ids, json_t *val);

/* Map over object with idset keys, calling 'map' for each id.
 * map function returns 0 on success, -1 with errno set to abort.
 */
typedef int (*rutil_idkey_map_f)(unsigned int id, json_t *val, void *arg);
int rutil_idkey_map (json_t *obj, rutil_idkey_map_f map, void *arg);

/* Merge obj2 into obj1, where both are objects with idset keys.
 */
int rutil_idkey_merge (json_t *obj1, json_t *obj2);

/* Count ranks represented in idkey object.
 */
int rutil_idkey_count (json_t *obj);


#endif /* !_FLUX_RESOURCE_RUTIL_H */


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
