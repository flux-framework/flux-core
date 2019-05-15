/************************************************************\
 * Copyright 2017 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_CF_H
#define _UTIL_CF_H

#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

// flags for cf_check
enum {
    CF_STRICT = 1,  // parse error on unknown keys
    CF_ANYTAB = 2,  // allow unknown keys for tables only
};

// allowed types
enum cf_type {
    CF_UNKNOWN = 0,
    CF_INT64 = 1,
    CF_DOUBLE = 2,
    CF_BOOL = 3,
    CF_STRING = 4,
    CF_TIMESTAMP = 5,
    CF_TABLE = 6,
    CF_ARRAY = 7,
};

typedef void cf_t;

/* cf_check option table consists of an array of struct cf_option
 * elements, terminated by CF_OPTIONS_TABLE_END.
 */
struct cf_option {
    const char *key;
    enum cf_type type;
    bool required;
};
#define CF_OPTIONS_TABLE_END \
    {                        \
        NULL, 0, false       \
    }

/* Error information is filled in by cf_update, cf_file, and cf_check.
 * If filename is unavailable, it is set to empty string.
 * If lineno is unavailable, it is set to -1.
 * The errbuf is always valid after one of the above functions fails.
 */
struct cf_error {
    char filename[PATH_MAX + 1];
    int lineno;
    char errbuf[200];
};

/* Create a cf_t object of type CF_TABLE.  Destroy with cf_destroy().
 */
cf_t *cf_create (void);
void cf_destroy (cf_t *cf);

/* Copy a cf_t object.  Destroy with cf_destroy().
 */
cf_t *cf_copy (const cf_t *cf);

/* Get type of cf_t object.
 */
enum cf_type cf_typeof (const cf_t *cf);

/* Get cf_t object from table.  Do not free.
 */
const cf_t *cf_get_in (const cf_t *cf, const char *key);

/* Get cf_t object from array.  Do not free.
 */
const cf_t *cf_get_at (const cf_t *cf, int index);

/* Access object as a particular type.
 * If wrong type or NULL cf, default value is returned.
 */
int64_t cf_int64 (const cf_t *cf);       // default: 0
double cf_double (const cf_t *cf);       // default: 0.
const char *cf_string (const cf_t *cf);  // default: ""
bool cf_bool (const cf_t *cf);           // default: false
time_t cf_timestamp (const cf_t *cf);    // default: 0

/* Get array size.
 * If object is not an array, return 0.
 */
int cf_array_size (const cf_t *cf);

/* Update table 'cf' with info parsed from TOML 'buf' or 'filename'.
 * On success return 0.  On failure, return -1 with errno set.
 * If error is non-NULL, write error description there.
 */
int cf_update (cf_t *cf, const char *buf, int len, struct cf_error *error);
int cf_update_file (cf_t *cf, const char *filename, struct cf_error *error);

/* Update table 'cf' with info parsed from all filenames matching the
 * wildcard 'pattern', following the rules for the glob(3) function call.
 * On success returns the number of individual files successfully parsed,
 * or 0 when there are no files that match 'pattern'.
 *
 * If any one file has a non-zero return code from cf_update_file(), then
 * the entire operation is aborted (no updates are written to 'cf'), and
 * -1 is returned with errno from cf_update_file() set.
 *
 * If error is non-NULL the description of the error is written there.
 */
int cf_update_glob (cf_t *cf, const char *pattern, struct cf_error *error);

/* Apply 'opts' to table 'cf' according to flags.
 * On success return 0.  On failure, return -1 with errno set.
 * If error is non-NULL, write error description there.
 */
int cf_check (const cf_t *cf,
              const struct cf_option opts[],
              int flags,
              struct cf_error *error);

#endif /* !_UTIL_CF_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
