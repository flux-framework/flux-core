/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <glob.h>
#include <jansson.h>
#include <flux/core.h>
#if !HAVE_JSON_OBJECT_UPDATE_RECURSIVE
#include "src/common/libmissing/json_object_update_recursive.h"
#endif

#include "src/common/libutil/errno_safe.h"
#include "src/common/libtomlc99/toml.h"
#include "src/common/libutil/tomltk.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/jpath.h"
#include "ccan/str/str.h"

#include "conf_private.h"
#include "conf.h"

struct flux_conf {
    json_t *obj;
    int refcount;
};

void flux_conf_decref (const flux_conf_t *conf_const)
{
    flux_conf_t *conf = (flux_conf_t *)conf_const;
    if (conf && --conf->refcount == 0) {
        ERRNO_SAFE_WRAP (json_decref, conf->obj);
        ERRNO_SAFE_WRAP (free, conf);
    }
}

const flux_conf_t *flux_conf_incref (const flux_conf_t *conf_const)
{
    flux_conf_t *conf = (flux_conf_t *)conf_const;

    if (conf)
        conf->refcount++;
    return conf;
}

flux_conf_t *flux_conf_create (void)
{
    flux_conf_t *conf;

    if (!(conf = calloc (1, sizeof (*conf))))
        return NULL;
    conf->refcount = 1;
    if (!(conf->obj = json_object ())) {
        flux_conf_decref (conf);
        errno = ENOMEM;
        return NULL;
    }
    return conf;
}

flux_conf_t *flux_conf_copy (const flux_conf_t *conf)
{
    flux_conf_t *cpy;

    if (!(cpy = flux_conf_create ()))
        return NULL;
    json_decref (cpy->obj);
    if (!(cpy->obj = json_deep_copy (conf->obj))) {
        flux_conf_decref (cpy);
        return NULL;
    }
    return cpy;
}

static int conf_update_obj (flux_conf_t *conf,
                            const char *filename,
                            json_t *obj,
                            flux_error_t *error)
{
    if (json_object_update_recursive (conf->obj, obj) < 0) {
        errprintf (error,
                   "%s: updating JSON object: out of memory",
                   filename);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static int conf_update_toml (flux_conf_t *conf,
                             const char *filename,
                             flux_error_t *error)
{
    struct tomltk_error toml_error;
    toml_table_t *tab;
    json_t *obj = NULL;

    if (!(tab = tomltk_parse_file (filename, &toml_error))) {
        if (toml_error.lineno == -1) {
            errprintf (error,
                       "%s: %s",
                       toml_error.filename,
                       toml_error.errbuf);
        }
        else {
            errprintf (error,
                       "%s:%d: %s",
                       toml_error.filename,
                       toml_error.lineno,
                       toml_error.errbuf);
        }
        goto error;
    }
    if (!(obj = tomltk_table_to_json (tab))) {
        errprintf (error,
                   "%s: converting TOML to JSON: %s",
                   filename,
                   strerror (errno));
        goto error;
    }
    if (conf_update_obj (conf, filename, obj, error) < 0)
        goto error;
    json_decref (obj);
    toml_free (tab);
    return 0;
error:
    ERRNO_SAFE_WRAP (toml_free, tab);
    ERRNO_SAFE_WRAP (json_decref, obj);
    return -1;
}

static int conf_update_json (flux_conf_t *conf,
                             const char *filename,
                             flux_error_t *error)
{
    json_error_t err;
    json_t *obj = NULL;

    if (!(obj = json_load_file (filename, 0, &err))) {
        errprintf (error, "%s:%d: %s", filename, err.line, err.text);
        goto error;
    }
    if (conf_update_obj (conf, filename, obj, error) < 0)
        goto error;
    json_decref (obj);
    return 0;
error:
    ERRNO_SAFE_WRAP (json_decref, obj);
    return -1;
}

static const char *file_extension (const char *path)
{
    const char *p;
    if (path && (p = strrchr (path,  '.')))
        return p + 1;
    return "";
}

static int conf_update (flux_conf_t *conf,
                        const char *filename,
                        flux_error_t *error)
{
    const char *ext = file_extension (filename);
    if (streq (ext, "json"))
        return conf_update_json (conf, filename, error);
    return conf_update_toml (conf, filename, error);
}

/* Parse s as a JSON object string and merge into conf.
 */
static int conf_update_inline_json (flux_conf_t *conf,
                                    const char *s,
                                    flux_error_t *error)
{
    json_error_t j_error;
    json_t *obj;
    int rc = -1;

    if (!(obj = json_loads (s, 0, &j_error))) {
        errprintf (error, "inline JSON: %s", j_error.text);
        errno = EINVAL;
        return -1;
    }
    /* Both conf->obj (always a json_object()) and obj (from json_loads()
     * on '{'-prefixed input) are guaranteed to be JSON objects, so this
     * should not fail.
     */
    if (json_object_update_recursive (conf->obj, obj) < 0) {
        errprintf (error, "merging inline JSON: internal error");
        errno = EINVAL;
        goto done;
    }
    rc = 0;
done:
    ERRNO_SAFE_WRAP (json_decref, obj);
    return rc;
}

/* Parse inline string (must contain a newline) and merge into conf.
 * If s starts with '{' (ignoring leading whitespace), parse as JSON;
 * otherwise parse as TOML.
 */
static int conf_update_inline (flux_conf_t *conf,
                               const char *s,
                               flux_error_t *error)
{
    struct tomltk_error toml_error;
    toml_table_t *tab = NULL;
    json_t *obj = NULL;
    int rc = -1;

    /* If value starts with '{' (possibly after leading whitespace e.g. from
     * a Python triple-quoted string), parse specifically as JSON.
     */
    if (s[strspn (s, " \t\n\r")] == '{')
        return conf_update_inline_json (conf, s, error);

    /* Fall back to TOML */
    if (!(tab = tomltk_parse (s, strlen (s), &toml_error))) {
        if (toml_error.lineno == -1)
            errprintf (error, "%s", toml_error.errbuf);
        else
            errprintf (error,
                       "line %d: %s",
                       toml_error.lineno,
                       toml_error.errbuf);
        errno = EINVAL;
        goto done;
    }
    if (!(obj = tomltk_table_to_json (tab))) {
        errprintf (error,
                   "converting inline TOML to JSON: %s",
                   strerror (errno));
        goto done;
    }
    /* Both conf->obj (always a json_object()) and obj (from
     * tomltk_table_to_json() on a TOML table) are guaranteed to be
     * JSON objects, so this should not fail.
     */
    if (json_object_update_recursive (conf->obj, obj) < 0) {
        errprintf (error, "merging inline TOML: internal error");
        errno = EINVAL;
        goto done;
    }
    rc = 0;
done:
    ERRNO_SAFE_WRAP (json_decref, obj);
    ERRNO_SAFE_WRAP (toml_free, tab);
    return rc;
}

/* Parse s as KEY=VAL where KEY is a dotted path and VAL is a JSON scalar or
 * a bare string. Apply to conf.
 */
static int conf_update_keyval (flux_conf_t *conf,
                               const char *s,
                               flux_error_t *error)
{
    const char *eq;
    char *key = NULL;
    json_t *val = NULL;
    int rc = -1;

    /* Note: `=` in `s` should already be guaranteed by caller.
     * Error message returned to caller reflects this
     */
    if (!(eq = strchr (s, '=')) || eq == s) {
        errprintf (error, "value must have a key in KEY=VAL");
        errno = EINVAL;
        goto done;
    }
    if (!(key = strndup (s, eq - s))) {
        errprintf (error, "%s", strerror (errno));
        goto done;
    }

    /* Try to parse value as JSON (includes true/false, numbers, strings):
     * Otherwise, fallback to parsing as a string.
     */
    if (!(val = json_loads (eq + 1, JSON_DECODE_ANY, NULL))) {
        if (!(val = json_string (eq + 1))) {
            errprintf (error, "out of memory");
            errno = ENOMEM;
            goto done;
        }
    }
    if (jpath_set (conf->obj, key, val) < 0) {
        errprintf (error, "setting %s: %s", key, strerror (errno));
        goto done;
    }
    rc = 0;
done:
    free (key);
    json_decref (val);
    return rc;
}

int flux_conf_update (flux_conf_t *conf,
                      const char *value,
                      flux_error_t *error)
{
    if (!conf || !value) {
        errno = EINVAL;
        errprintf (error, "%s", strerror (errno));
        return -1;
    }
    if (strchr (value, '\n'))
        return conf_update_inline (conf, value, error);
    if (value[0] == '{')
        return conf_update_inline_json (conf, value, error);
    if (strchr (value, '='))
        return conf_update_keyval (conf, value, error);
    return conf_update (conf, value, error);
}

struct globerr {
    int rc;
    const char *msg;
    int errnum;
};

struct globerr globerr_tab[] = {
    { GLOB_NOMATCH, "No match",              ENOENT },
    { GLOB_NOSPACE, "Out of memory",         ENOMEM },
    { GLOB_ABORTED, "Read error",            EINVAL },
    { 0,            "Unknown glob error",    EINVAL },
};

/* Set errno and optionally 'error' based on glob error return code.
 */
void conf_globerr (flux_error_t *error, const char *pattern, int rc)
{
    struct globerr *entry;
    for (entry = &globerr_tab[0]; entry->rc != 0; entry++) {
        if (rc == entry->rc)
            break;
    }
    errprintf (error, "%s: %s", pattern, entry->msg);
    errno = entry->errnum;
}

static flux_conf_t *conf_parse_dir (const char *path, flux_error_t *error)
{
    flux_conf_t *conf;
    glob_t gl;
    int rc;
    size_t i;
    char pattern[4096];

    if (access (path, R_OK | X_OK) < 0) {
        errprintf (error, "%s: %s", path, strerror (errno));
        return NULL;
    }
    rc = snprintf (pattern, sizeof (pattern), "%s/*.toml", path);
    if (rc >= sizeof (pattern)) {
        errno = EOVERFLOW;
        errprintf (error, "%s: %s", path, strerror (errno));
        return NULL;
    }
    if (!(conf = flux_conf_create ())) {
        errprintf (error, "%s: %s", path, strerror (errno));
        return NULL;
    }
    rc = glob (pattern, GLOB_ERR, NULL, &gl);
    if (rc == 0) {
        for (i = 0; i < gl.gl_pathc; i++) {
            if (conf_update_toml (conf, gl.gl_pathv[i], error) < 0)
                goto error_glob;
        }
        globfree (&gl);
    }
    else if (rc != GLOB_NOMATCH) {
        conf_globerr (error, pattern, rc);
        goto error;
    }
    return conf;

error_glob:
    ERRNO_SAFE_WRAP (globfree, &gl);
error:
    flux_conf_decref (conf);
    return NULL;
}

static flux_conf_t *conf_parse_file (const char *path, flux_error_t *error)
{
    flux_conf_t *conf;

    if (!(conf = flux_conf_create ())) {
        errprintf (error, "%s: %s", path, strerror (errno));
        return NULL;
    }
    if (conf_update (conf, path, error) < 0) {
        flux_conf_decref (conf);
        return NULL;
    }
    return conf;
}

flux_conf_t *flux_conf_parse (const char *path, flux_error_t *error)
{
    struct stat st;

    if (!path) {
        errno = EINVAL;
        errprintf (error, "%s", strerror (errno));
        return NULL;
    }
    if (stat (path, &st) < 0) {
        errprintf (error, "stat: %s: %s", path, strerror (errno));
        return NULL;
    }
    if (S_ISDIR(st.st_mode))
        return conf_parse_dir (path, error);
    return conf_parse_file (path, error);
}

int flux_conf_vunpack (const flux_conf_t *conf,
                       flux_error_t *error,
                       const char *fmt,
                       va_list ap)
{
    json_error_t j_error;

    if (!conf || !conf->obj || !fmt || *fmt == '\0') {
        errno = EINVAL;
        errprintf (error, "%s", strerror (errno));
        return -1;
    }
    if (json_vunpack_ex (conf->obj, &j_error, 0, fmt, ap) < 0) {
        /* N.B. Cannot translate JSON error back to TOML file/line,
         * so only the text[] portion of the JSON error is used here.
         */
        errprintf (error, "%s", j_error.text);
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_conf_unpack (const flux_conf_t *conf,
                      flux_error_t *error,
                      const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = flux_conf_vunpack (conf, error, fmt, ap);
    va_end (ap);

    return rc;
}

flux_conf_t *flux_conf_vpack (const char *fmt, va_list ap)
{
    flux_conf_t *conf;

    if (!fmt) {
        errno = EINVAL;
        return NULL;
    }
    if (!(conf = flux_conf_create ()))
        return NULL;
    json_decref (conf->obj);
    if (!(conf->obj = json_vpack_ex (NULL, 0, fmt, ap))) {
        flux_conf_decref (conf);
        errno = EINVAL;
        return NULL;
    }

    return conf;
}

flux_conf_t *flux_conf_pack (const char *fmt, ...)
{
    va_list ap;
    flux_conf_t *conf;

    if (!fmt) {
        errno = EINVAL;
        return NULL;
    }
    va_start (ap, fmt);
    conf = flux_conf_vpack (fmt, ap);
    va_end (ap);

    return conf;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
