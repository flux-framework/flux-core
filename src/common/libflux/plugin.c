/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
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

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fnmatch.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <uuid.h>
#include <jansson.h>
#include <assert.h>
#include <flux/core.h>
#include <pthread.h>
#include <flux/core.h>

#include "src/common/libflux/plugin_private.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/aux.h"
#include "ccan/str/str.h"

#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37     // defined in later libuuid headers
#endif

static int use_deepbind = 1;
static pthread_once_t deepbind_once = PTHREAD_ONCE_INIT;

static void init_use_deepbind (void)
{
    const char *var = getenv ("FLUX_LOAD_WITH_DEEPBIND");
    if (!var) {
        // default is to allow the flag
        return;
    }
    use_deepbind = strtol (var, NULL, 10);
}

int plugin_deepbind (void)
{
    pthread_once (&deepbind_once, init_use_deepbind);
    return use_deepbind != 0 ? FLUX_DEEPBIND : 0;
}

struct flux_plugin {
    char *path;
    char *name;
    json_t *conf;
    char *conf_str;
    struct aux_item *aux;
    void *dso;
    zlistx_t *handlers;
    int flags;
    char last_error [128];
    uuid_t uuid;
    char uuid_str[UUID_STR_LEN];
};

struct flux_plugin_arg {
    json_error_t error;
    json_t * in;
    json_t * out;
};

typedef const struct flux_plugin_handler *
        (*find_handler_f) (flux_plugin_t *p, const char *topic);

static void flux_plugin_handler_destroy (struct flux_plugin_handler *h)
{
    if (h) {
        free ((char *) h->topic);
        free (h);
    }
}

static void handler_free (void **item)
{
    if (*item) {
        struct flux_plugin_handler *h = *item;
        flux_plugin_handler_destroy (h);
        *item = NULL;
    }
}

static const struct flux_plugin_handler * find_handler (flux_plugin_t *p,
                                                        const char *string)
{
    struct flux_plugin_handler *h = zlistx_first (p->handlers);
    while (h) {
        if (streq (h->topic, string))
            return h;
        h = zlistx_next (p->handlers);
    }
    return NULL;
}

static const struct flux_plugin_handler * match_handler (flux_plugin_t *p,
                                                         const char *string)
{
    struct flux_plugin_handler *h = zlistx_first (p->handlers);
    while (h) {
        if (fnmatch (h->topic, string, 0) == 0)
            return h;
        h = zlistx_next (p->handlers);
    }
    return NULL;
}

static struct flux_plugin_handler *
flux_plugin_handler_create (const char *topic, flux_plugin_f cb, void *arg)
{
    struct flux_plugin_handler *h = calloc (1, sizeof (*h));
    if (!h || !(h->topic = strdup (topic)))
        goto error;
    h->cb = cb;
    h->data = arg;
    return (h);
error:
    flux_plugin_handler_destroy (h);
    return NULL;
}

void flux_plugin_destroy (flux_plugin_t *p)
{
    if (p) {
        int saved_errno = errno;
        json_decref (p->conf);
        zlistx_destroy (&p->handlers);
        free (p->conf_str);
        free (p->path);
        free (p->name);
        aux_destroy (&p->aux);
#ifndef __SANITIZE_ADDRESS__
        if (p->dso)
            dlclose (p->dso);
#endif
        free (p);
        errno = saved_errno;
    }
}

static int plugin_seterror (flux_plugin_t *p, int errnum, const char *fmt, ...)
{
    if (p && fmt) {
        va_list ap;
        va_start (ap, fmt);
        vsnprintf (p->last_error, sizeof (p->last_error), fmt, ap);
        va_end (ap);
    }
    else if (p) {
        snprintf (p->last_error,
                  sizeof (p->last_error),
                  "%s", strerror (errno));
    }
    errno = errnum;
    return -1;
}

static inline void plugin_error_clear (flux_plugin_t *p)
{
    if (p)
        p->last_error [0] = '\0';
}

flux_plugin_t *flux_plugin_create (void)
{
    flux_plugin_t *p = calloc (1, sizeof (*p));
    if (!p || !(p->handlers = zlistx_new ())) {
        flux_plugin_destroy (p);
        return NULL;
    }
    p->flags = FLUX_PLUGIN_RTLD_LAZY;
    uuid_generate (p->uuid);
    uuid_unparse (p->uuid, p->uuid_str);
    zlistx_set_destructor (p->handlers, handler_free);
    return p;
}

static int flags_invalid (int flags)
{
    const int valid_flags =
        FLUX_PLUGIN_RTLD_LAZY
        | FLUX_PLUGIN_RTLD_NOW
        | FLUX_PLUGIN_RTLD_GLOBAL
        | FLUX_PLUGIN_RTLD_DEEPBIND;
    return (flags & ~valid_flags);
}

int flux_plugin_set_flags (flux_plugin_t *p, int flags)
{
    if (!p || flags_invalid (flags)) {
        errno = EINVAL;
        return -1;
    }
    p->flags = flags;
    return 0;
}

int flux_plugin_get_flags (flux_plugin_t *p)
{
    if (p) {
        return p->flags;
    }
    return 0;
}

int flux_plugin_set_name (flux_plugin_t *p, const char *name)
{
    char *new = NULL;
    plugin_error_clear (p);
    if (!p || !name)
        return plugin_seterror (p, EINVAL, NULL);
    if (!(new = strdup (name)))
        return -1;
    free (p->name);
    p->name = new;
    return 0;
}

const char * flux_plugin_get_name (flux_plugin_t *p)
{
    plugin_error_clear (p);
    if (!p) {
        errno = EINVAL;
        return NULL;
    }
    return p->name;
}

const char * flux_plugin_get_uuid (flux_plugin_t *p)
{
    plugin_error_clear (p);
    if (!p) {
        errno = EINVAL;
        return NULL;
    }
    return p->uuid_str;
}

const char * flux_plugin_get_path (flux_plugin_t *p)
{
    if (p)
        return p->path;
    return NULL;
}

int flux_plugin_aux_set (flux_plugin_t *p,
                         const char *key,
                         void *val,
                         aux_free_f free_fn)
{
    return aux_set (&p->aux, key, val, free_fn);
}

void *flux_plugin_aux_get (flux_plugin_t *p, const char *key)
{
    return aux_get (p->aux, key);
}

void flux_plugin_aux_delete (flux_plugin_t *p, const void *val)
{
    return aux_delete (&p->aux, val);
}

const char *flux_plugin_strerror (flux_plugin_t *p)
{
    return p->last_error;
}

static int open_flags (flux_plugin_t *p)
{
    int flags = 0;
    if ((p->flags & FLUX_PLUGIN_RTLD_LAZY))
        flags |= RTLD_LAZY;
    if ((p->flags & FLUX_PLUGIN_RTLD_NOW))
        flags |= RTLD_NOW;
    if ((p->flags & FLUX_PLUGIN_RTLD_GLOBAL))
        flags |= RTLD_GLOBAL;
    else
        flags |= RTLD_LOCAL;
    if ((p->flags & FLUX_PLUGIN_RTLD_DEEPBIND))
        flags |= plugin_deepbind ();
    return flags;
}

int flux_plugin_load_dso (flux_plugin_t *p, const char *path)
{
    flux_plugin_init_f init;
    plugin_error_clear (p);
    if (!p || !path)
        return plugin_seterror (p, EINVAL, NULL);
    if (access (path, R_OK) < 0)
        return plugin_seterror (p, errno, "%s: %s", path, strerror (errno));
    dlerror ();
    if (!(p->dso = dlopen (path, open_flags (p))))
        return plugin_seterror (p, errno, "dlopen: %s", dlerror ());

    free (p->path);
    free (p->name);
    if (!(p->path = strdup (path)) || !(p->name = strdup (path)))
        return plugin_seterror (p, ENOMEM, NULL);

    if ((init = dlsym (p->dso, "flux_plugin_init"))
        && (*init) (p) < 0)
        return plugin_seterror (p, 0, "%s: flux_plugin_init failed", path);
    return 0;
}

int flux_plugin_set_conf (flux_plugin_t *p, const char *json_str)
{
    json_error_t err;
    plugin_error_clear (p);
    if (!p || !json_str)
        return plugin_seterror (p, EINVAL, NULL);
    if (!(p->conf = json_loads (json_str, 0, &err))) {
        return plugin_seterror (p,
                                errno,
                                "parse error: col %d: %s",
                                err.column,
                                err.text);
    }
    if (p->conf_str) {
        free (p->conf_str);
        p->conf_str = NULL;
    }
    return 0;
}

const char *flux_plugin_get_conf (flux_plugin_t *p)
{
    if (!p) {
        plugin_seterror (p, EINVAL, NULL);
        return NULL;
    }
    if (!p->conf_str) {
        if (!p->conf) {
            plugin_seterror (p, ENOENT, "No plugin conf set");
            return NULL;
        }
        p->conf_str = json_dumps (p->conf, JSON_ENCODE_ANY|JSON_COMPACT);
        if (!p->conf_str) {
            plugin_seterror (p,
                             errno,
                             "json_dumps failed: %s",
                             strerror (errno));
            return NULL;
        }
    }
    return p->conf_str;
}

int flux_plugin_conf_unpack (flux_plugin_t *p, const char *fmt, ...)
{
    json_error_t err;
    va_list ap;
    int rc;
    plugin_error_clear (p);
    if (!p || !fmt)
        return plugin_seterror (p, EINVAL, NULL);
    if (!p->conf)
        return plugin_seterror (p, ENOENT, "No plugin conf set");
    va_start (ap, fmt);
    rc = json_vunpack_ex (p->conf, &err, 0, fmt, ap);
    va_end (ap);
    if (rc < 0)
        return plugin_seterror (p, errno, "unpack error: %s", err.text);
    return rc;
}

int flux_plugin_remove_handler (flux_plugin_t *p,
                                const char *topic)
{
    plugin_error_clear (p);
    if (!p || !topic)
        return plugin_seterror (p, EINVAL, NULL);
    if (find_handler (p, topic)) {
        if (zlistx_delete (p->handlers, zlistx_cursor (p->handlers)) < 0)
            return plugin_seterror (p, errno, NULL);
    }
    return 0;
}

static flux_plugin_f get_handler (flux_plugin_t *p,
                                  const char *topic,
                                  find_handler_f fn)
{
    const struct flux_plugin_handler *h;
    plugin_error_clear (p);
    if (!p || !topic) {
        plugin_seterror (p, EINVAL, NULL);
        return NULL;
    }
    if ((h = (*fn) (p, topic)))
        return h->cb;
    return NULL;
}

flux_plugin_f flux_plugin_get_handler (flux_plugin_t *p, const char *topic)
{
    return get_handler (p, topic, find_handler);
}

flux_plugin_f flux_plugin_match_handler (flux_plugin_t *p, const char *topic)
{
    return get_handler (p, topic, match_handler);
}


int flux_plugin_add_handler (flux_plugin_t *p,
                             const char *topic,
                             flux_plugin_f cb,
                             void *arg)
{
    struct flux_plugin_handler *h = NULL;
    plugin_error_clear (p);
    if (!p || !topic)
        return plugin_seterror (p, EINVAL, NULL);

    if (!cb)
        return flux_plugin_remove_handler (p, topic);

    if (!(h = flux_plugin_handler_create (topic, cb, arg)))
        return plugin_seterror (p, errno, NULL);

    if (!(zlistx_add_end (p->handlers, h))) {
        flux_plugin_handler_destroy (h);
        return plugin_seterror (p, errno, NULL);
    }

    return 0;
}

int flux_plugin_register (flux_plugin_t *p,
                          const char *name,
                          const struct flux_plugin_handler t[])
{
    plugin_error_clear (p);
    if (!p || !t)
        return plugin_seterror (p, EINVAL, NULL);
    if (name && flux_plugin_set_name (p, name))
        return -1;
    while (t->topic) {
        if (flux_plugin_add_handler (p, t->topic, t->cb, t->data) < 0)
            return -1;
        t++;
    }
    return 0;
}

static int arg_seterror (flux_plugin_arg_t *arg,
                         int errnum,
                         const char *fmt,
                         ...)
{
    if (fmt) {
        va_list ap;
        va_start (ap, fmt);
        vsnprintf (arg->error.text, sizeof (arg->error.text), fmt, ap);
        va_end (ap);
    }
    else if (arg) {
        snprintf (arg->error.text,
                  sizeof (arg->error.text),
                  "%s",
                  strerror (errno));
    }
    errno = errnum;
    return -1;
}

static inline void arg_clear_error (flux_plugin_arg_t *arg)
{
    if (arg)
        arg->error.text[0] = '\0';
}

const char *flux_plugin_arg_strerror (flux_plugin_arg_t *args)
{
    if (!args)
        return strerror (errno);
    return args->error.text;
}

void flux_plugin_arg_destroy (flux_plugin_arg_t *args)
{
    if (args) {
        json_decref (args->in);
        json_decref (args->out);
        free (args);
    }
}

flux_plugin_arg_t *flux_plugin_arg_create (void)
{
    flux_plugin_arg_t *args = calloc (1, sizeof (*args));
    return args;
}

static json_t **arg_get (flux_plugin_arg_t *args, int flags)
{
    return ((flags & FLUX_PLUGIN_ARG_OUT) ? &args->out : &args->in);
}

static int arg_set (flux_plugin_arg_t *args, int flags, json_t *o)
{
    json_t **dstp;
    dstp = arg_get (args, flags);
    if (!(flags & FLUX_PLUGIN_ARG_REPLACE) && *dstp != NULL) {
        /*  On update, the object 'o' is spiritually inherited by
         *   args, so decref this object after attempting the update.
         */
        int rc = json_object_update (*dstp, o);
        json_decref (o);
        return rc;
    }
    json_decref (*dstp);
    *dstp = o;
    return 0;
}

int flux_plugin_arg_set (flux_plugin_arg_t *args, int flags,
                         const char *json_str)
{
    json_t *o = NULL;
    arg_clear_error (args);
    if (!args)
        return arg_seterror (args, EINVAL, NULL);
    if (json_str && !(o = json_loads (json_str, 0, &args->error)))
        return -1;
    return arg_set (args, flags, o);
}

int flux_plugin_arg_get (flux_plugin_arg_t *args, int flags, char **json_str)
{
    json_t **op;
    arg_clear_error (args);
    if (!args || !json_str)
        return arg_seterror (args, EINVAL, NULL);
    op = arg_get (args, flags);
    if (*op == NULL)
        return arg_seterror (args, ENOENT, "No args currently set");
    *json_str = json_dumps (*op, JSON_COMPACT);
    return 0;
}

int flux_plugin_arg_vpack (flux_plugin_arg_t *args,
                           int flags,
                           const char *fmt,
                           va_list ap)
{
    json_t *o;
    arg_clear_error (args);
    if (!args || !fmt)
        return arg_seterror (args, EINVAL, NULL);
    if (!(o = json_vpack_ex (&args->error, 0, fmt, ap)))
        return -1;
    return arg_set (args, flags, o);
}

int flux_plugin_arg_pack (flux_plugin_arg_t *args,
                          int flags,
                          const char *fmt,
                          ...)
{
    int rc;
    va_list ap;
    va_start (ap, fmt);
    rc = flux_plugin_arg_vpack (args, flags, fmt, ap);
    va_end (ap);
    return rc;
}

int flux_plugin_arg_vunpack (flux_plugin_arg_t *args,
                             int flags,
                             const char *fmt,
                             va_list ap)
{
    json_t **op;
    arg_clear_error (args);
    if (!fmt || !args)
        return arg_seterror (args, EINVAL, NULL);
    op = arg_get (args, flags);
    return json_vunpack_ex (*op, &args->error, 0, fmt, ap);
}

int flux_plugin_arg_unpack (flux_plugin_arg_t *args,
                            int flags,
                            const char *fmt,
                            ...)
{
    int rc;
    va_list ap;
    va_start (ap, fmt);
    rc = flux_plugin_arg_vunpack (args, flags, fmt, ap);
    va_end (ap);
    return rc;
}

int flux_plugin_call (flux_plugin_t *p,
                      const char *string,
                      flux_plugin_arg_t *args)
{
    const struct flux_plugin_handler *h = NULL;
    plugin_error_clear (p);
    if (!p || !string)
        return plugin_seterror (p, EINVAL, NULL);
    if (!(h = match_handler (p, string)))
        return 0;
    assert (h->cb);
    if ((*h->cb) (p, string, args, h->data) < 0)
        return -1;
    return 1;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
