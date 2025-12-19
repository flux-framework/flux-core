/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* upmi.c - universal pmi client for internal use */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>
#include <assert.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif
#include <glob.h>

#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libczmqcontainers/czmq_containers.h"

#include "upmi.h"
#include "upmi_plugin.h"

struct upmi {
    zlist_t *plugins;
    flux_plugin_t *plugin;
    flux_plugin_arg_t *args;
    char *searchpath;
    size_t searchpath_len;
    char *methods;
    size_t methods_len;
    char *name;
    void *ctx;
    int flags;
    upmi_trace_f trace_fun;
    void *trace_arg;
};

void upmi_trace (struct upmi *upmi, const char *fmt, ...);
static int upmi_preinit (struct upmi *upmi,
                         int flags,
                         json_t *args,
                         const char *path,
                         const char **note,
                         flux_error_t *error);

int upmi_simple_init (flux_plugin_t *p);
int upmi_libpmi2_init (flux_plugin_t *p);
int upmi_libpmi_init (flux_plugin_t *p);
int upmi_config_init (flux_plugin_t *p);
int upmi_single_init (flux_plugin_t *p);

static flux_plugin_init_f builtins[] = {
    &upmi_simple_init,
    &upmi_libpmi2_init,
    &upmi_libpmi_init,
    &upmi_config_init,
    &upmi_single_init,
};

static const char *default_methods = "config simple libpmi2 libpmi single";

void upmi_destroy (struct upmi *upmi)
{
    if (upmi) {
        int saved_errno = errno;
        free (upmi->methods);
        free (upmi->searchpath);
        free (upmi->name);
        flux_plugin_arg_destroy (upmi->args);
        zlist_destroy (&upmi->plugins);
        free (upmi);
        errno = saved_errno;
    }
}

static int upmi_plugin_append (struct upmi *upmi, flux_plugin_t *p)
{
    if (zlist_append (upmi->plugins, p) < 0)
        return -1;
    zlist_freefn (upmi->plugins, p, (zlist_free_fn *)flux_plugin_destroy, true);
    return 0;
}

static int upmi_register_external_glob (struct upmi *upmi,
                                        const char *pattern,
                                        flux_error_t *error)
{
    glob_t pglob;
    int rc;

    if ((rc = glob (pattern, 0, NULL, &pglob)) != 0) {
        if (rc == GLOB_NOMATCH)
            return 0;
        return errprintf (error,
                          "plugin glob error: %s",
                          rc == GLOB_NOSPACE ? "out of memory" :
                          rc == GLOB_ABORTED ? "read error" : "?");
    }
    for (int i = 0; i < pglob.gl_pathc; i++) {
        flux_plugin_t *p;

        if (!(p = flux_plugin_create ()))
            goto nomem;
        if (flux_plugin_load_dso (p, pglob.gl_pathv[i]) < 0) {
            errprintf (error, "%s", flux_plugin_strerror (p));
            flux_plugin_destroy (p);
            goto error;
        }
        if (upmi_plugin_append (upmi, p) < 0) {
            flux_plugin_destroy (p);
            goto nomem;
        }
    }
    globfree (&pglob);
    return 0;
nomem:
    errprintf (error, "out of memory");
error:
    globfree (&pglob);
    return -1;
}

static int upmi_register_external (struct upmi *upmi, flux_error_t *error)
{
    const char *item;
    char pattern[1024];

    item = argz_next (upmi->searchpath, upmi->searchpath_len, NULL);
    while (item) {
        snprintf (pattern, sizeof (pattern), "%s/*.so", item);
        if (upmi_register_external_glob (upmi, pattern, error) < 0)
            return -1;
        item = argz_next (upmi->searchpath, upmi->searchpath_len, item);
    }
    return 0;
}

static int upmi_register_builtin (struct upmi *upmi)
{
    for (int i = 0; i < ARRAY_SIZE (builtins); i++) {
        flux_plugin_init_f plugin_init = builtins[i];
        flux_plugin_t *p;

        if (!(p = flux_plugin_create ())
            || plugin_init (p) < 0
            || upmi_plugin_append (upmi, p) < 0) {
            flux_plugin_destroy (p);
            return -1;
        }
    }
    return 0;
}

/* Instantiate 'struct upmi' without selecting a plugin.
 */
static struct upmi *upmi_create_uninit (const char *methods,
                                        const char *searchpath,
                                        int flags,
                                        upmi_trace_f trace_fun,
                                        void *trace_arg,
                                        flux_error_t *error)
{
    struct upmi *upmi;

    if (flags & ~(UPMI_TRACE | UPMI_LIBPMI_NOFLUX | UPMI_LIBPMI2_CRAY)) {
        errprintf (error, "invalid argument");
        return NULL;
    }
    if (!(upmi = calloc (1, sizeof (*upmi)))
        || !(upmi->plugins = zlist_new ())
        || (argz_create_sep (searchpath,
                             ':',
                             &upmi->searchpath,
                             &upmi->searchpath_len) != 0)
        || argz_create_sep (methods,
                            ' ',
                            &upmi->methods,
                            &upmi->methods_len) != 0) {
        errprintf (error, "out of memory");
        goto error;
    }
    upmi->flags = flags;
    upmi->trace_fun = trace_fun;
    upmi->trace_arg = trace_arg;
    if (upmi_register_builtin (upmi) < 0) {
        errprintf (error, "error registering builtin plugins");
        goto error;
    }
    return upmi;
error:
    upmi_destroy (upmi);
    return NULL;
}

static bool match_scheme (const char *scheme, const char *uri)
{
    size_t slen = strlen (scheme);
    if (slen < strlen (uri)
        && uri[slen] == ':'
        && strstarts (uri, scheme))
            return true;
    return streq (uri, scheme);
}

/* Search for a plugin by name.
 * If a plugin name is not found among pre-loaded builtin plugins,
 * load the external plugins and search again.
 */
static flux_plugin_t *lookup_plugin (struct upmi *upmi,
                                     const char *uri,
                                     flux_error_t *error)
{
    flux_plugin_t *p;
    bool loaded = false;
again:
    p = zlist_first (upmi->plugins);
    while (p) {
        if (match_scheme (flux_plugin_get_name (p), uri))
            break;
        p = zlist_next (upmi->plugins);
    }
    if (!p && !loaded && upmi->searchpath) {
        if (upmi_register_external (upmi, error) < 0)
            return NULL;
        loaded = true;
        goto again;
    }
    if (!p)
        errprintf (error, "plugin matching '%s' not found", uri);
    return p;
}

struct upmi *upmi_create_ex (const char *uri,
                             int flags,
                             json_t *args,
                             upmi_trace_f trace_fun,
                             void *trace_arg,
                             flux_error_t *errp)
{
    struct upmi *upmi;
    const char *searchpath;
    const char *methods;

    if (!(methods = getenv ("FLUX_PMI_CLIENT_METHODS")))
        methods = default_methods;
    if (!(searchpath = getenv ("FLUX_PMI_CLIENT_SEARCHPATH")))
        searchpath = flux_conf_builtin_get ("upmi_pluginpath", FLUX_CONF_AUTO);

    if (!(upmi = upmi_create_uninit (methods,
                                     searchpath,
                                     flags,
                                     trace_fun,
                                     trace_arg,
                                     errp)))
        return NULL;

    if (uri) {
        const char *path;
        const char *note;

        if (!(upmi->plugin = lookup_plugin (upmi, uri, errp)))
            goto error;
        if ((path = strchr (uri, ':')))
            path++;
        if (upmi_preinit (upmi, upmi->flags, args, path, &note, errp) < 0)
            goto error;
        if (note != NULL)
            upmi_trace (upmi, "%s", note);
    }
    else {
        flux_error_t error;
        const char *note;

        uri = argz_next (upmi->methods, upmi->methods_len, NULL);
        while (uri) {
            upmi_trace (upmi, "trying '%s'", uri);
            if ((upmi->plugin = lookup_plugin (upmi, uri, &error))
                && upmi_preinit (upmi,
                                 upmi->flags,
                                 args,
                                 NULL,
                                 &note,
                                 &error) == 0) {
                upmi_trace (upmi, "%s", note ? note : "selected");
                break;
            }
            upmi_trace (upmi, "%s", error.text);
            errprintf (errp, "%s", error.text);
            upmi->plugin = NULL;
            uri = argz_next (upmi->methods, upmi->methods_len, uri);
        }
        if (!upmi->plugin) // 'error' was filled by last plugin in methods list
            goto error;
    }
    return upmi;
error:
    upmi_destroy (upmi);
    return NULL;
}

struct upmi *upmi_create (const char *uri,
                          int flags,
                          upmi_trace_f trace_fun,
                          void *trace_arg,
                          flux_error_t *errp)
{
    return upmi_create_ex (uri, flags, NULL, trace_fun, trace_arg, errp);
}

const char *upmi_describe (struct upmi *upmi)
{
    const char *name = NULL;
    if (upmi)
        name = flux_plugin_get_name (upmi->plugin);
    return name;
}

/* Invoke plugin callback.
 * This destroys and recreates upmi->args with IN arguments based on (fmt, ap).
 * After the successful call, upmi->args contains OUT arguments.
 */
static int upmi_vcall (struct upmi *upmi,
                       const char *name,
                       flux_error_t *error,
                       const char *fmt,
                       va_list ap)
{
    int rc;

    flux_plugin_arg_destroy (upmi->args);
    if (!(upmi->args = flux_plugin_arg_create ())) {
        errprintf (error, "out of memory");
        return -1;
    }
    if (fmt) {
        if (flux_plugin_arg_vpack (upmi->args,
                                   FLUX_PLUGIN_ARG_IN,
                                   fmt,
                                   ap) < 0) {
            errprintf (error, "%s", flux_plugin_arg_strerror (upmi->args));
            return -1;
        }
    }
    rc = flux_plugin_call (upmi->plugin, name, upmi->args);
    if (rc == 0) {
        errprintf (error, "%s not implemented", name);
        return -1;
    }
    if (rc < 0) {
        const char *errmsg;
        if (flux_plugin_arg_unpack (upmi->args,
                                    FLUX_PLUGIN_ARG_OUT,
                                    "{s:s}",
                                    "errmsg", &errmsg) < 0)
            errmsg = strerror (errno != 0 ? errno : EINVAL);
        errprintf (error, "%s", errmsg);
        return -1;
    }
    return 0;
}

static int upmi_call (struct upmi *upmi,
                      const char *name,
                      flux_error_t *error,
                      const char *fmt,
                      ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = upmi_vcall (upmi, name, error, fmt, ap);
    va_end (ap);
    return rc;
}

static int upmi_preinit (struct upmi *upmi,
                         int flags,
                         json_t *args,
                         const char *path,
                         const char **notep,
                         flux_error_t *error)
{
    json_t *payload;
    int rc;

    if (!(payload = json_object ()))
        goto nomem;
    if ((flags & UPMI_LIBPMI_NOFLUX)) {
        if (json_object_set (payload, "noflux", json_true ()) < 0)
            goto nomem;
    }
    if ((flags & UPMI_LIBPMI2_CRAY)) {
        if (json_object_set (payload, "craycray", json_true ()) < 0)
            goto nomem;
    }
    if (path) {
        json_t *o;
        if (!(o = json_string (path))
            || json_object_set_new (payload, "path", o) < 0) {
            json_decref (o);
            goto nomem;
        }
    }
    if (args) {
        if (!json_is_object (args)) {
            errprintf (error, "arguments must be a json object");
            errno = EINVAL;
            goto error;
        }
        const char *key;
        json_t *value;
        json_object_foreach (args, key, value) {
            if (json_object_get (payload, key) != NULL) {
                errprintf (error,
                           "preinit argument '%s' conflicts with builtin",
                           key);
                errno = EEXIST;
                goto error;

            }
            if (json_object_set (payload, key, value) < 0)
                goto nomem;
        }
    }
    rc = upmi_call (upmi, "upmi.preinit", error, "O", payload);
    json_decref (payload);
    if (rc == 0 && notep) {
        const char *note = NULL;
        (void)flux_plugin_arg_unpack (upmi->args,
                                      FLUX_PLUGIN_ARG_OUT,
                                      "{s:s}",
                                      "note", &note);
        *notep = note;
    }
    return rc;
nomem:
    errno = ENOMEM;
error:
    ERRNO_SAFE_WRAP (json_decref, payload);
    return -1;
}

int upmi_initialize (struct upmi *upmi,
                     struct upmi_info *infop,
                     flux_error_t *errp)
{
    struct upmi_info info;
    const char *name;
    flux_error_t error;

    if (!upmi) {
        errprintf (errp, "invalid argument");
        return -1;
    }
    if (upmi_call (upmi, "upmi.initialize", &error, NULL) < 0) {
        errprintf (errp, "%s", error.text);
        upmi_trace (upmi, "initialize: %s", error.text);
        return -1;
    }
    info.dict = NULL;
    if (flux_plugin_arg_unpack (upmi->args,
                                FLUX_PLUGIN_ARG_OUT,
                                "{s:i s:s s:i s?o}",
                                "rank", &info.rank,
                                "name", &name,
                                "size", &info.size,
                                "dict", &info.dict) < 0) {
        errprintf (errp, "%s", flux_plugin_arg_strerror (upmi->args));
        return -1;
    }
    free (upmi->name);
    if (!(upmi->name = strdup (name))) {
        errprintf (errp, "out of memory");
        return -1;
    }
    info.name = upmi->name;
    upmi_trace (upmi,
                "initialize: rank=%d size=%d name=%s: success",
                info.rank,
                info.size,
                info.name);
    if (infop)
        *infop = info;
    return 0;
}

int upmi_finalize (struct upmi *upmi, flux_error_t *errp)
{
    flux_error_t error;

    if (!upmi) {
        errprintf (errp, "invalid argument");
        return -1;
    }
    if (upmi_call (upmi, "upmi.finalize", &error, NULL) < 0) {
        errprintf (errp, "%s", error.text);
        upmi_trace (upmi, "finalize: %s", error.text);
        return -1;
    }
    upmi_trace (upmi, "finalize: success");
    return 0;
}

int upmi_abort (struct upmi *upmi, const char *msg, flux_error_t *errp)
{
    flux_error_t error;

    if (!upmi || !msg) {
        errprintf (errp, "invalid argument\n");
        return -1;
    }
    if (upmi_call (upmi,
                   "upmi.abort",
                   &error,
                   "{s:s}",
                   "msg", msg) < 0) {
        errprintf (errp, "%s", error.text);
        upmi_trace (upmi, "abort: %s", error.text);
        return -1;
    }
    // possibly not reached
    upmi_trace (upmi, "abort: success");
    return 0;
}

int upmi_put (struct upmi *upmi,
              const char *key,
              const char *value,
              flux_error_t *errp)
{
    flux_error_t error;

    if (!upmi || !key || !value) {
        errprintf (errp, "invalid argument");
        return -1;
    }
    if (upmi_call (upmi,
                   "upmi.put",
                   &error,
                   "{s:s s:s}",
                   "key", key,
                   "value", value) < 0) {
        errprintf (errp, "%s", error.text);
        upmi_trace (upmi, "put key=%s: %s", key, error.text);
        return -1;
    }
    upmi_trace (upmi, "put key=%s value=%s: success", key, value);
    return 0;
}

int upmi_get (struct upmi *upmi,
              const char *key,
              int rank,
              char **valuep,
              flux_error_t *errp)
{
    flux_error_t error;
    const char *value;
    char *cpy;

    if (!upmi || !key || !valuep) {
        errprintf (errp, "invalid argument");
        return -1;
    }
    if (upmi_call (upmi,
                   "upmi.get",
                   &error,
                   "{s:s s:i}",
                   "key", key,
                   "rank", rank) < 0) {
        errprintf (errp, "%s", error.text);
        upmi_trace (upmi, "get key=%s: %s", key, error.text);
        return -1;
    }
    if (flux_plugin_arg_unpack (upmi->args,
                                FLUX_PLUGIN_ARG_OUT,
                                "{s:s}",
                                "value", &value) < 0) {
        errprintf (errp, "%s", flux_plugin_arg_strerror (upmi->args));
        return -1;
    }
    upmi_trace (upmi, "get key=%s value=%s: success", key, value);
    if (!(cpy = strdup (value))) {
        errprintf (errp, "out of memory");
        return -1;
    }
    *valuep = cpy;
    return 0;
}

int upmi_barrier (struct upmi *upmi, flux_error_t *errp)
{
    flux_error_t error;

    if (!upmi) {
        errprintf (errp, "invalid argument");
        return -1;
    }
    if (upmi_call (upmi, "upmi.barrier", &error, NULL) < 0) {
        errprintf (errp, "%s", error.text);
        upmi_trace (upmi, "barrier: %s", error.text);
        return -1;
    }
    upmi_trace (upmi, "barrier: success");
    return 0;
}

static void upmi_vtrace (struct upmi *upmi, const char *fmt, va_list ap)
{
    if ((upmi->flags & UPMI_TRACE) && upmi->trace_fun) {
        char buf[1024];
        const char *prefix = upmi_describe (upmi);
        snprintf (buf,
                  sizeof (buf),
                  "%s: ",
                  prefix ? prefix : "flux-pmi-client");
        size_t len = strlen (buf);
        vsnprintf (buf + len, sizeof (buf) - len, fmt, ap);
        upmi->trace_fun (upmi->trace_arg, buf);
    }
}

void upmi_trace (struct upmi *upmi, const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    upmi_vtrace (upmi, fmt, ap);
    va_end (ap);
}

static void upmi_setverror (flux_plugin_t *p,
                            flux_plugin_arg_t *args,
                            const char *fmt,
                            va_list ap)
{
    char buf[128];

    (void)vsnprintf (buf, sizeof (buf), fmt, ap);
    (void)flux_plugin_arg_pack (args,
                                FLUX_PLUGIN_ARG_OUT,
                                "{s:s}",
                                "errmsg", buf);
}

int upmi_seterror (flux_plugin_t *p,
                   flux_plugin_arg_t *args,
                   const char *fmt,
                   ...)
{
    va_list ap;

    va_start (ap, fmt);
    upmi_setverror (p, args, fmt, ap);
    va_end (ap);
    return -1;
}

// vi:ts=4 sw=4 expandtab
