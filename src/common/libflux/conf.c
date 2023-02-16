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
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"

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
    { "cmdhelp_pattern",INSTALLED_CMDHELP_PATTERN,  INTREE_CMDHELP_PATTERN },
    { "pmi_library_path",
                        INSTALLED_PMI_LIBRARY_PATH, INTREE_PMI_LIBRARY_PATH },
    { "shell_path",     INSTALLED_SHELL_PATH,       INTREE_SHELL_PATH },
    { "shell_pluginpath",
                        INSTALLED_SHELL_PLUGINPATH, INTREE_SHELL_PLUGINPATH },
    { "shell_initrc",   INSTALLED_SHELL_INITRC,     INTREE_SHELL_INITRC },
    { "jobtap_pluginpath",
                        INSTALLED_JOBTAP_PLUGINPATH,INTREE_JOBTAP_PLUGINPATH },
    { "upmi_pluginpath",INSTALLED_UPMI_PLUGINPATH,  INTREE_UPMI_PLUGINPATH },
    { "no_docs_path",   INSTALLED_NO_DOCS_PATH,     INTREE_NO_DOCS_PATH },
    { "rundir",         INSTALLED_RUNDIR,           NULL },
    { "bindir",         INSTALLED_BINDIR,           INTREE_BINDIR },
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
        if (name && streq (entry->key, name)) {
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
    if (json_object_update (conf->obj, obj) < 0) {
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
    if (conf_update_toml (conf, path, error) < 0) {
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

int flux_conf_reload_decode (const flux_msg_t *msg, const flux_conf_t **confp)
{
    const char *auxkey = "flux::conf";
    json_t *o;
    flux_conf_t *conf;

    if (!(conf = flux_msg_aux_get (msg, auxkey))) {
        if (flux_request_unpack (msg, NULL, "o", &o) < 0)
            return -1;
        if (!(conf = flux_conf_create ()))
            return -1;
        json_decref (conf->obj);
        conf->obj = json_incref (o);
        if (flux_msg_aux_set (msg, auxkey, (flux_conf_t *)conf, (flux_free_f)flux_conf_decref) < 0) {
            flux_conf_decref (conf);
            return -1;
        }
    }
    if (confp)
        *confp = conf;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
