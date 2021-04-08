/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_GRUDGESET_H
#define _UTIL_GRUDGESET_H

/*  "grudge" set implementation. A set which only allows values to
 *   be inserted once, even if they are subsequently removed.
 */

struct grudgeset;

/* Add the string 'val' to set `*gsetp`. If *gsetp is NULL
 * a new grudge set will be created with a single entry.
 *
 *  If a value was previously added to the set, then the append
 *   will fail with -1 and errno set to EEXIST.
 */
int grudgeset_add (struct grudgeset **gsetp, const char *val);

/*  Remove matching entry 'val' from gset. It is assumed there are
 *   no duplicates.
 */
int grudgeset_remove (struct grudgeset *gset, const char *val);

/*  Return number of elements in grudgeset
 *  If gset is NULL then the size is 0.
 */
int grudgeset_size (struct grudgeset *gset);

/*  Return 1 if val has been used in grudgeset, 0 otherwise
 */
int grudgeset_used (struct grudgeset *gset, const char *val);

/*  Return 1 if set currently contains val, 0 otherwise
 */
int grudgeset_contains (struct grudgeset *gset, const char *val);

/*  Return the current set in grudgeset 'gset'
 *  The returned object is a JSON array.
 *  Note: this should not be modified by caller, but is returned
 *   non-const so it can be json_incref'd for nocopy use in message
 *   payloads, etc.
 */
json_t *grudgeset_tojson (struct grudgeset *gset);

void grudgeset_destroy (struct grudgeset *gset);

#endif /* !_UTIL_GRUDGE_SET_H */
