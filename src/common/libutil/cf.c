/************************************************************\
 * Copyright 2017 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */
#include <errno.h>
#include <string.h>
#include <glob.h>
#include <jansson.h>

#include "src/common/libtomlc99/toml.h"
#include "tomltk.h"
#include "cf.h"

#define ERRBUFSZ 200

cf_t *cf_create (void)
{
    cf_t *cf;

    if (!(cf = json_object ())) {
        errno = ENOMEM;
        return NULL;
    }
    return cf;
}

void cf_destroy (cf_t *cf)
{
    if (cf) {
        json_decref (cf);
    }
}

cf_t *cf_copy (const cf_t *cf)
{
    cf_t *cpy;

    if (!cf) {
        errno = EINVAL;
        return NULL;
    }
    if (!(cpy = json_deep_copy (cf))) {
        errno = ENOMEM;
        return NULL;
    }
    return cpy;
}

static void __attribute__ ((format (printf, 4, 5))) errprintf (struct cf_error *error,
                                                               const char *filename,
                                                               int lineno,
                                                               const char *fmt,
                                                               ...)
{
    va_list ap;
    int saved_errno = errno;

    if (error) {
        memset (error, 0, sizeof (*error));
        va_start (ap, fmt);
        (void)vsnprintf (error->errbuf, sizeof (error->errbuf), fmt, ap);
        va_end (ap);
        if (filename)
            strncpy (error->filename, filename, PATH_MAX);
        error->lineno = lineno;
    }
    errno = saved_errno;
}

struct typedesc {
    enum cf_type type;
    const char *desc;
};

static const struct typedesc typetab[] = {
    {CF_INT64, "int64"},
    {CF_DOUBLE, "double"},
    {CF_BOOL, "bool"},
    {CF_STRING, "string"},
    {CF_TIMESTAMP, "timestamp"},
    {CF_TABLE, "table"},
    {CF_ARRAY, "array"},
};
const int typetablen = sizeof (typetab) / sizeof (typetab[0]);

const char *cf_typedesc (enum cf_type type)
{
    int i;
    for (i = 0; i < typetablen; i++)
        if (typetab[i].type == type)
            return typetab[i].desc;
    return "unknown";
}

enum cf_type cf_typeof (const cf_t *cf)
{
    if (!cf)
        return CF_UNKNOWN;
    switch (json_typeof ((json_t *)cf)) {
        case JSON_OBJECT:
            if (tomltk_json_to_epoch (cf, NULL) == 0)
                return CF_TIMESTAMP;
            else
                return CF_TABLE;
        case JSON_ARRAY:
            return CF_ARRAY;
        case JSON_INTEGER:
            return CF_INT64;
        case JSON_REAL:
            return CF_DOUBLE;
        case JSON_TRUE:
        case JSON_FALSE:
            return CF_BOOL;
        case JSON_STRING:
            return CF_STRING;
        default:
            return CF_UNKNOWN;
    }
}

const cf_t *cf_get_in (const cf_t *cf, const char *key)
{
    cf_t *val;

    if (!cf || cf_typeof (cf) != CF_TABLE || !key) {
        errno = EINVAL;
        return NULL;
    }
    if (!(val = json_object_get (cf, key))) {
        errno = ENOENT;
        return NULL;
    }
    return val;
}

const cf_t *cf_get_at (const cf_t *cf, int index)
{
    cf_t *val;

    if (!cf || cf_typeof (cf) != CF_ARRAY || index < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(val = json_array_get (cf, index))) {
        errno = ENOENT;
        return NULL;
    }
    return val;
}

int64_t cf_int64 (const cf_t *cf)
{
    return cf ? json_integer_value (cf) : 0;
}

double cf_double (const cf_t *cf)
{
    return cf ? json_real_value (cf) : 0.;
}

const char *cf_string (const cf_t *cf)
{
    const char *s = cf ? json_string_value (cf) : NULL;
    return s ? s : "";
}

bool cf_bool (const cf_t *cf)
{
    if (cf && json_typeof ((json_t *)cf) == JSON_TRUE)
        return true;
    return false;
}

time_t cf_timestamp (const cf_t *cf)
{
    time_t t;
    if (!cf || tomltk_json_to_epoch (cf, &t) < 0)
        return 0;
    return t;
}

int cf_array_size (const cf_t *cf)
{
    return cf ? json_array_size (cf) : 0;
}

/* Parse some TOML and merge it with 'cf' object.
 * If filename is non-NULL, take TOML from file, o/w use buf, len.
 */
static int update_object (cf_t *cf,
                          const char *filename,
                          const char *buf,
                          int len,
                          struct cf_error *error)
{
    struct tomltk_error toml_error;
    toml_table_t *tab;
    json_t *obj = NULL;
    int saved_errno;

    if (!cf || json_typeof ((json_t *)cf) != JSON_OBJECT) {
        errprintf (error, filename, -1, "invalid config object");
        errno = EINVAL;
        return -1;
    }
    if (filename)
        tab = tomltk_parse_file (filename, &toml_error);
    else
        tab = tomltk_parse (buf, len, &toml_error);
    if (!tab) {
        errprintf (error,
                   toml_error.filename,
                   toml_error.lineno,
                   "%s",
                   toml_error.errbuf);
        goto error;
    }
    if (!(obj = tomltk_table_to_json (tab))) {
        errprintf (error,
                   filename,
                   -1,
                   "converting TOML to JSON: %s",
                   strerror (errno));
        goto error;
    }
    if (json_object_update (cf, obj) < 0) {
        errprintf (error, filename, -1, "updating JSON object: out of memory");
        errno = ENOMEM;
        goto error;
    }
    json_decref (obj);
    toml_free (tab);
    return 0;
error:
    saved_errno = errno;
    toml_free (tab);
    json_decref (obj);
    errno = saved_errno;
    return -1;
}

int cf_update (cf_t *cf, const char *buf, int len, struct cf_error *error)
{
    return update_object (cf, NULL, buf, len, error);
}

int cf_update_file (cf_t *cf, const char *filename, struct cf_error *error)
{
    return update_object (cf, filename, NULL, 0, error);
}

int cf_update_glob (cf_t *cf, const char *pattern, struct cf_error *error)
{
    cf_t *tmp;
    glob_t gl;
    size_t i;
    int count = -1;
    int errnum = 0;
    int rc = glob (pattern, GLOB_ERR, NULL, &gl);

    tmp = cf_create ();

    switch (rc) {
        case 0:
            count = 0;
            for (i = 0; i < gl.gl_pathc; i++) {
                if (cf_update_file (tmp, gl.gl_pathv[i], error) < 0) {
                    errnum = errno;
                    count = -1;
                    break;
                }
                count++;
            }
            break;
        case GLOB_NOMATCH:
            count = 0;
            errprintf (error, pattern, -1, "No match");
            break;
        case GLOB_NOSPACE:
            errprintf (error, pattern, -1, "Out of memory");
            errnum = ENOMEM;
            break;
        case GLOB_ABORTED:
            errprintf (error, pattern, -1, "Read error");
            errnum = EINVAL;
            break;
    }
    globfree (&gl);

    /*
     *  If glob sucessfully processed at least one file, update
     *   caller's cf object with all new date in tmp object:
     */
    if ((count > 0) && json_object_update (cf, tmp) < 0) {
        errprintf (error, pattern, -1, "updating JSON object: out of memory");
        errno = ENOMEM;
        count = -1;
    }
    cf_destroy (tmp);
    errno = errnum;
    return (count);
}

static bool is_end_marker (struct cf_option opt)
{
    const struct cf_option end = CF_OPTIONS_TABLE_END;
    return (opt.key == end.key && opt.type == end.type && opt.required == end.required);
}

static const struct cf_option *find_option (const struct cf_option opts[],
                                            const char *key)
{
    int i;
    if (opts) {
        for (i = 0; !is_end_marker (opts[i]); i++) {
            if (!strcmp (opts[i].key, key))
                return &opts[i];
        }
    }
    return NULL;
}

/* Make sure all keys in 'cf' are known in 'opts'.
 * If 'anytab' is true, keys representing tables need not be known.
 */
static int check_unknown_keys (const cf_t *cf,
                               const struct cf_option opts[],
                               bool anytab,
                               struct cf_error *error)
{
    void *iter;

    /* N.B. const is cast away for iteration, but object is not modified
     * so this should be safe.
     */
    iter = json_object_iter ((json_t *)cf);
    while (iter) {
        const char *key = json_object_iter_key (iter);
        json_t *obj = json_object_get (cf, key);

        if (!find_option (opts, key)) {
            if (!json_is_object (obj) || !anytab) {
                errprintf (error, NULL, -1, "key '%s' is unknown", key);
                errno = EINVAL;
                return -1;
            }
        }
        iter = json_object_iter_next ((json_t *)cf, iter);
    }
    return 0;
}

static int check_expected_keys (const cf_t *cf,
                                const struct cf_option opts[],
                                struct cf_error *error)
{
    int i;

    if (opts) {
        for (i = 0; !is_end_marker (opts[i]); i++) {
            json_t *obj = json_object_get (cf, opts[i].key);

            if (!obj && opts[i].required) {
                errprintf (error, NULL, -1, "'%s' must be set", opts[i].key);
                errno = EINVAL;
                return -1;
            }
            if (obj && cf_typeof (obj) != opts[i].type) {
                errprintf (error,
                           NULL,
                           -1,
                           "'%s' must be of type %s",
                           opts[i].key,
                           cf_typedesc (opts[i].type));
                errno = EINVAL;
                return -1;
            }
        }
    }
    return 0;
}

int cf_check (const cf_t *cf,
              const struct cf_option opts[],
              int flags,
              struct cf_error *error)
{
    if (!cf || json_typeof ((json_t *)cf) != JSON_OBJECT) {
        errprintf (error, NULL, -1, "invalid config object");
        errno = EINVAL;
        return -1;
    }
    if ((flags & CF_STRICT)) {
        if (check_unknown_keys (cf, opts, (flags & CF_ANYTAB), error) < 0)
            return -1;
    }
    if (check_expected_keys (cf, opts, error) < 0)
        return -1;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
