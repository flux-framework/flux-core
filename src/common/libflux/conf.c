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

#include "src/common/libutil/intree.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libtomlc99/toml.h"
#include "src/common/libutil/tomltk.h"

#include "conf.h"
#include "conf_private.h"

struct builtin {
    char *key;
    const char *val_installed;
    const char *val_intree;
};

struct flux_conf {
    json_t *obj;
    int refcount;
};

static const char *conf_auxkey = "flux::conf_object";

static struct builtin builtin_tab[] = {
    { "lua_cpath_add",  INSTALLED_LUA_CPATH_ADD,    INTREE_LUA_CPATH_ADD },
    { "lua_path_add",   INSTALLED_LUA_PATH_ADD,     INTREE_LUA_PATH_ADD },
    { "python_path",    INSTALLED_PYTHON_PATH,      INTREE_PYTHON_PATH },
    { "man_path",       INSTALLED_MAN_PATH,         INTREE_MAN_PATH },
    { "exec_path",      INSTALLED_EXEC_PATH,        INTREE_EXEC_PATH },
    { "connector_path", INSTALLED_CONNECTOR_PATH,   INTREE_CONNECTOR_PATH },
    { "module_path",    INSTALLED_MODULE_PATH,      INTREE_MODULE_PATH },
    { "rc1_path",       INSTALLED_RC1_PATH,         INTREE_RC1_PATH },
    { "rc3_path",       INSTALLED_RC3_PATH,         INTREE_RC3_PATH },
    { "cf_path",        INSTALLED_CF_PATH,          INTREE_CF_PATH },
    { "cmdhelp_pattern",INSTALLED_CMDHELP_PATTERN,  INTREE_CMDHELP_PATTERN },
    { "pmi_library_path",
                        INSTALLED_PMI_LIBRARY_PATH, INTREE_PMI_LIBRARY_PATH },
    { "shell_path",     INSTALLED_SHELL_PATH,       INTREE_SHELL_PATH },
    { "shell_pluginpath",
                        INSTALLED_SHELL_PLUGINPATH, INTREE_SHELL_PLUGINPATH },
    { "shell_initrc",   INSTALLED_SHELL_INITRC,     INTREE_SHELL_INITRC },
    { "keydir",         NULL,                       INTREE_KEYDIR },
    { "no_docs_path",   INSTALLED_NO_DOCS_PATH,     INTREE_NO_DOCS_PATH },
    { "rundir",         INSTALLED_RUNDIR,           NULL },
    { "bindir",         INSTALLED_BINDIR,           INTREE_BINDIR },
    { "jobspec_validate_path", INSTALLED_JOBSPEC_VALIDATE_PATH,
                                            INTREE_JOBSPEC_VALIDATE_PATH },
    { "jobspec_validator_args", INSTALLED_JOBSPEC_VALIDATOR_ARGS,
                                            INTREE_JOBSPEC_VALIDATOR_ARGS },
    { NULL, NULL, NULL },
};

const char *flux_conf_builtin_get (const char *name,
                                   enum flux_conf_flags flags)
{
    struct builtin *entry;
    bool intree = false;
    const char *val = NULL;

    switch (flags) {
        case FLUX_CONF_INSTALLED:
            break;
        case FLUX_CONF_INTREE:
            intree = true;
            break;
        case FLUX_CONF_AUTO:
            /* In this context, if executable_is_intree() returns -1 due to
             * an unlikely internal error, we return the installed value.
             */
            if (executable_is_intree () == 1)
                intree = true;
            break;
    }
    for (entry = &builtin_tab[0]; entry->key != NULL; entry++) {
        if (name && !strcmp (entry->key, name)) {
            val = intree ? entry->val_intree : entry->val_installed;
            break;
        }
    }
    if (!val)
        goto error;
    return val;
error:
    errno = EINVAL;
    return NULL;
}

static void __attribute__ ((format (printf, 4, 5)))
errprintf (flux_conf_error_t *error,
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
            strncpy (error->filename, filename, sizeof (error->filename) - 1);
        error->lineno = lineno;
    }
    errno = saved_errno;
}

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

static int conf_update (flux_conf_t *conf,
                        const char *filename,
                        flux_conf_error_t *error)
{
    struct tomltk_error toml_error;
    toml_table_t *tab;
    json_t *obj = NULL;

    if (!(tab = tomltk_parse_file (filename, &toml_error))) {
        errprintf (error,
                   toml_error.filename,
                   toml_error.lineno,
                   "%s",
                   toml_error.errbuf);
        goto error;
    }
    if (!(obj = tomltk_table_to_json (tab))) {
        errprintf (error, filename, -1, "converting TOML to JSON: %s",
                   strerror (errno));
        goto error;
    }
    if (json_object_update (conf->obj, obj) < 0) {
        errno = ENOMEM;
        errprintf (error,
                   filename,
                   -1,
                   "updating JSON object: %s",
                   strerror (errno));
        goto error;
    }
    json_decref (obj);
    toml_free (tab);
    return 0;
error:
    ERRNO_SAFE_WRAP (toml_free, tab);
    ERRNO_SAFE_WRAP (json_decref, obj);
    return -1;
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
void conf_globerr (flux_conf_error_t *error, const char *pattern, int rc)
{
    struct globerr *entry;
    for (entry = &globerr_tab[0]; entry->rc != 0; entry++) {
        if (rc == entry->rc)
            break;
    }
    errprintf (error, pattern, -1, "%s", entry->msg);
    errno = entry->errnum;
}

flux_conf_t *flux_conf_parse (const char *path, flux_conf_error_t *error)
{
    flux_conf_t *conf;
    glob_t gl;
    int rc;
    size_t i;
    char pattern[4096];

    if (!path) {
        errno = EINVAL;
        errprintf (error, path, -1, "%s", strerror (errno));
        return NULL;
    }
    if (access (path, R_OK | X_OK) < 0) {
        errprintf (error, path, -1, "%s", strerror (errno));
        return NULL;
    }
    rc = snprintf (pattern, sizeof (pattern), "%s/*.toml", path);
    if (rc >= sizeof (pattern)) {
        errno = EOVERFLOW;
        errprintf (error, path, -1, "%s", strerror (errno));
        return NULL;
    }
    if (!(conf = flux_conf_create ())) {
        errprintf (error, path, -1, "%s", strerror (errno));
        return NULL;
    }
    rc = glob (pattern, GLOB_ERR, NULL, &gl);
    if (rc == 0) {
        for (i = 0; i < gl.gl_pathc; i++) {
            if (conf_update (conf, gl.gl_pathv[i], error) < 0)
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

int flux_set_conf (flux_t *h, const flux_conf_t *conf)
{
    return flux_aux_set (h,
                         conf_auxkey,
                         (flux_conf_t *)conf,
                         conf ? (flux_free_f)flux_conf_decref : NULL);
}

const flux_conf_t *flux_get_conf (flux_t *h)
{
    return flux_aux_get (h, conf_auxkey);
}

int flux_conf_vunpack (const flux_conf_t *conf,
                       flux_conf_error_t *error,
                       const char *fmt,
                       va_list ap)
{
    json_error_t j_error;

    if (!conf || !conf->obj || !fmt || *fmt == '\0') {
        errno = EINVAL;
        errprintf (error, NULL, -1, "%s", strerror (errno));
        return -1;
    }
    if (json_vunpack_ex (conf->obj, &j_error, 0, fmt, ap) < 0) {
        /* N.B. Cannot translate JSON error back to TOML file/line,
         * so only the text[] portion of the JSON error is used here.
         */
        errprintf (error, NULL, -1, "%s", j_error.text);
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_conf_unpack (const flux_conf_t *conf,
                      flux_conf_error_t *error,
                      const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = flux_conf_vunpack (conf, error, fmt, ap);
    va_end (ap);

    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
