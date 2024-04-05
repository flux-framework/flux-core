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

/* Compare 'old_set' to 'new_set'.
 * Create '*add' for ids in new_set but not in old_set (sets NULL if n/a).
 * Create '*sub' for ids in old_set but not in new_set (sets NULL if n/a).
 */
int rutil_idset_diff (const struct idset *old_set,
                      const struct idset *new_set,
                      struct idset **add,
                      struct idset **sub);

/* Set key=val in a json object, where val is the string
 * representation of 'ids', or the empty string if 'ids' is NULL.
 */
int rutil_set_json_idset (json_t *o,
                          const char *key,
                          const struct idset *ids);

/* Load data by path:
 * - rutil_read_file() returns data as a NULL-terminated string.
 * - rutil_load_file() parses data as a JSON object and returns it.
 * - rutil_load_xml_dir() parses <rank>.xml files in path, and returns
 *   a JSON object with ranks as keys and XML strings as values.
 * On error put human readable error in errbuf and return NULL
 */
char *rutil_read_file (const char *path, flux_error_t *errp);
json_t *rutil_load_file (const char *path, flux_error_t *errp);
json_t *rutil_load_xml_dir (const char *path, flux_error_t *errp);

/* Map over object with idset keys, calling 'map' for each id.
 * map function returns 0 on success, -1 with errno set to abort.
 */
typedef int (*rutil_idkey_map_f)(unsigned int id, json_t *val, void *arg);
int rutil_idkey_map (json_t *obj, rutil_idkey_map_f map, void *arg);

/* Count ranks represented in idkey object.
 */
int rutil_idkey_count (json_t *obj);

#endif /* !_FLUX_RESOURCE_RUTIL_H */


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
