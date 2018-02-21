#ifndef _UTIL_TOMLTK_H
#define _UTIL_TOMLTK_H

/* tomltk - toolkit for tomlc99 and jansson */

#include <jansson.h>
#include <time.h>
#include <limits.h>
#include "src/common/libtomlc99/toml.h"

struct tomltk_error {
    char filename[PATH_MAX + 1];
    int lineno;
    char errbuf[200];
};

// TOML timestamp is represented as JSON object like this:
//   { "iso-8601-ts" : "2003-08-24T05:14:50Z" }

/* Convert TOML table to a JSON object.
 * Return new object on success, NULL on failure with errno set.
 */
json_t *tomltk_table_to_json (toml_table_t *tab);

/* Convert timestamp JSON object to a time_t (UTC).
 * Return 0 on success, or -1 on failure with errno set.
 */
int tomltk_json_to_epoch (const json_t *obj, time_t *t);

/* Convert time_t (UTC) to a timestamp JSON object.
 * Return new object on success, NULL on failure with errno set.
 */
json_t *tomltk_epoch_to_json (time_t t);

/* Convert TOML timestamp to a time_t (UTC)
 * Return 0 on success, or -1 on failure with errno set.
 */
int tomltk_ts_to_epoch (toml_timestamp_t *ts, time_t *tp);

/* Wrapper for toml_parse() that internally copies 'conf',
 * adding NULL termination.  On success, 0 is returned.
 * On failure -1 is returned with errno set.  If 'error' is
 * non-NULL, an error description is written there.
 */
toml_table_t *tomltk_parse (const char *conf, int len,
                            struct tomltk_error *error);

/* Wrapper for toml_parse_file() that internally
 * opens/closes 'filename'.  On success, 0 is returned.
 * On failure -1 is returned with errno set.  If 'error' is
 * non-NULL, and error description is written there.
 */
toml_table_t *tomltk_parse_file (const char *filename,
                                 struct tomltk_error *error);

#endif /* !_UTIL_TOMLTK_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
