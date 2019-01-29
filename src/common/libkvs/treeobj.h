/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_KVS_TREEOBJ_H
#define _FLUX_KVS_TREEOBJ_H

#include <jansson.h>
#include <stdbool.h>

/* See RFC 11 */

/* Create a treeobj
 * valref, dirref: if blobref is NULL, treeobj_append_blobref()
 * must be called before object is valid.
 * val: copies argument (caller retains ownership)
 * symlink: ns argument is optional
 * Return JSON object on success, NULL on failure with errno set.
 */
json_t *treeobj_create_symlink (const char *ns, const char *target);
json_t *treeobj_create_val (const void *data, int len);
json_t *treeobj_create_valref (const char *blobref);
json_t *treeobj_create_dir (void);
json_t *treeobj_create_dirref (const char *blobref);

/* Validate treeobj, recursively.
 * Return 0 if valid, -1 with errno = EINVAL if invalid.
 */
int treeobj_validate (const json_t *obj);

/* get type (RFC 11 defined strings or NULL on error with errno set).
 */
const char *treeobj_get_type (const json_t *obj);

/* Test for a particular type
 */
bool treeobj_is_symlink (const json_t *obj);
bool treeobj_is_val (const json_t *obj);
bool treeobj_is_valref (const json_t *obj);
bool treeobj_is_dir (const json_t *obj);
bool treeobj_is_dirref (const json_t *obj);

/* get type-specific value.
 * For dirref/valref, this is an array of blobrefs.
 * For directory, this is dictionary of treeobjs
 * For symlink, this is an object with optinoal namespace and target.
 * For val this is string containing base64-encoded data.
 * Return JSON object on success, NULL on error with errno = EINVAL.
 * The returned object is owned by 'obj' and must not be destroyed.
 */
json_t *treeobj_get_data (json_t *obj);

/* get convenience functions, operate on type specific objects
 */
int treeobj_get_symlink (const json_t *obj,
                         const char **ns,
                         const char **target);

/* get decoded val data.
 * If len == 0, data will be NULL.
 * If len > 0, data will be followed by an extra NULL byte in memory.
 * Caller must free returned data.
 */
int treeobj_decode_val (const json_t *obj, void **data, int *len);

/* get type-specific count.
 * For dirref/valref, this is the number of blobrefs.
 * For directory, this is number of entries
 * For symlink or val, this is 1.
 * Return count on success, -1 on error with errno = EINVAL.
 */
int treeobj_get_count (const json_t *obj);

/* get/add/remove directory entry
 * Get returns JSON object (owned by 'obj', do not destory), NULL on error.
 * insert takes a reference on 'obj2' (caller retains ownership).
 * insert/delete return 0 on success, -1 on error with errno set.
 */
json_t *treeobj_get_entry (json_t *obj, const char *name);
int treeobj_insert_entry (json_t *obj, const char *name, json_t *obj2);
int treeobj_delete_entry (json_t *obj, const char *name);

/* peek directory entry
 * identical to treeobj_get_entry(), but is a const equivalent.  Good
 * to use when modifications will not occur.
 */
const json_t *treeobj_peek_entry (const json_t *obj, const char *name);

/* Shallow copy a treeobj
 * Note that this is not a shallow copy on the json object, but is a
 * shallow copy on the data within a tree object.  For example, for a
 * dir object, the first level of directory entries will be copied.
 */
json_t *treeobj_copy (json_t *obj);

/* Deep copy a treeobj */
json_t *treeobj_deep_copy (const json_t *obj);

/* add blobref to dirref,valref object.
 * Return 0 on success, -1 on failure with errno set.
 */
int treeobj_append_blobref (json_t *obj, const char *blobref);

/* get blobref entry at 'index'.
 * Return blobref on success, NULL on failure with errno set.
 */
const char *treeobj_get_blobref (const json_t *obj, int index);

/* Create valref that refers to 'data', a blob of 'len' bytes using
 * 'hashtype' hash algorithm (e.g. "sha1").  If 'maxblob' > 0, split the
 * blob into maxblob size chunks.
 */
json_t *treeobj_create_valref_buf (const char *hashtype, int maxblob,
                                   void *data, int len);

/* Convert a treeobj to/from string.
 * The return value of treeobj_decode must be destroyed with json_decref().
 * The return value of treeobj_encode must be destroyed with free().
 */
json_t *treeobj_decode (const char *buf);
json_t *treeobj_decodeb (const char *buf, size_t buflen);
char *treeobj_encode (const json_t *obj);

#endif /* !_FLUX_KVS_TREEOBJ_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
