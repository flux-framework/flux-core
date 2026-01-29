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
#include <limits.h>
#include <jansson.h>
#include <inttypes.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "attr.h"

struct registered_attr {
    const char *name;
    int flags;
};

struct broker_attr {
    zhash_t *hash;
    flux_msg_handler_t **handlers;
};

struct entry {
    char *name;
    char *val;
    int flags;
    attr_set_f set;
    attr_get_f get;
    void *arg;
};

struct registered_attr attrtab[] = {

    // general
    { "rank", ATTR_READONLY },
    { "size", ATTR_READONLY },
    { "version", ATTR_READONLY },
    { "rundir", 0 },
    { "rundir-cleanup", 0 },
    { "statedir", 0 },
    { "statedir-cleanup", 0 },
    { "security.owner", ATTR_READONLY },
    { "local-uri", 0 },
    { "parent-uri", ATTR_READONLY },
    { "instance-level", ATTR_READONLY },
    { "jobid", ATTR_READONLY },
    { "jobid-path", ATTR_READONLY },
    { "parent-kvs-namespace", ATTR_READONLY },
    { "hostlist", 0 },
    { "hostname", 0 },
    { "broker.mapping", ATTR_READONLY },
    { "broker.critical-ranks", 0 },
    { "broker.boot-method", 0 },
    { "broker.pid", ATTR_READONLY },
    { "broker.quorum", 0 },
    { "broker.quorum-warn", 0 },
    { "broker.shutdown-warn", 0 },
    { "broker.shutdown-timeout", 0 },
    { "broker.cleanup-timeout", 0 },
    { "broker.rc1_path", 0 },
    { "broker.rc3_path", 0 },
    { "broker.rc2_none", 0 },
    { "broker.rc2_pgrp", 0 },
    { "broker.exit-restart", ATTR_RUNTIME },
    { "broker.module-nopanic", ATTR_RUNTIME },
    { "broker.starttime", ATTR_READONLY },
    { "broker.sd-notify", 0 },
    { "broker.sd-stop-timeout", 0 },
    { "broker.exit-norestart", 0 },
    { "broker.recovery-mode", 0 },
    { "broker.uuid", ATTR_READONLY },
    { "conf.shell_initrc", ATTR_RUNTIME },
    { "conf.shell_pluginpath", ATTR_RUNTIME },
    { "config.path", 0 },

    // tree based overlay network
    { "tbon.topo", ATTR_CONFIG },
    { "tbon.descendants", ATTR_READONLY },
    { "tbon.level", ATTR_READONLY },
    { "tbon.maxlevel", ATTR_READONLY },
    { "tbon.endpoint", ATTR_READONLY },
    { "tbon.parent-endpoint", ATTR_READONLY },
    { "tbon.zmqdebug", ATTR_CONFIG },
    { "tbon.zmq_io_threads", ATTR_CONFIG },
    { "tbon.child_rcvhwm", ATTR_CONFIG },
    { "tbon.prefertcp", 0 },
    { "tbon.interface-hint", ATTR_RUNTIME | ATTR_CONFIG },
    { "tbon.torpid_min", ATTR_CONFIG },
    { "tbon.torpid_max", ATTR_CONFIG },
    { "tbon.tcp_user_timeout", ATTR_CONFIG },
    { "tbon.connect_timeout", ATTR_CONFIG },

    // logging
    { "log-ring-size", ATTR_RUNTIME },
    { "log-forward-level", ATTR_RUNTIME },
    { "log-critical-level", ATTR_RUNTIME },
    { "log-filename", ATTR_RUNTIME },
    { "log-syslog-enable", ATTR_RUNTIME },
    { "log-syslog-level", ATTR_RUNTIME },
    { "log-stderr-mode", ATTR_RUNTIME },
    { "log-stderr-level", ATTR_RUNTIME },
    { "log-level", ATTR_RUNTIME },

    // content
    { "content.backing-module", 0 },
    { "content.hash", 0 },
    { "content.dump", 0 },
    { "content.restore", 0 },

    // cron
    { "cron.directory", 0 },

    // for testing
    { "test.*", ATTR_RUNTIME },
    { "test-ro.*", ATTR_READONLY },

    // misc undocumented
    { "vendor.*", ATTR_RUNTIME}, // flux-framework/flux-pmix#109
    { "tbon.fanout", 0 }, // legacy, replaced by tbon.topo
};

static void entry_destroy (void *arg)
{
    struct entry *e = arg;
    if (e) {
        int saved_errno = errno;
        free (e->val);
        free (e->name);
        free (e);
        errno = saved_errno;
    }
}

static struct entry *entry_create (const char *name, const char *val, int flags)
{
    struct entry *e;
    if (!(e = calloc (1, sizeof (*e))))
        return NULL;

    if (name && !(e->name = strdup (name)))
        goto cleanup;
    if (val && !(e->val = strdup (val)))
        goto cleanup;
    e->flags = flags;
    return e;
cleanup:
    entry_destroy (e);
    return NULL;
}

int attr_delete (attr_t *attrs, const char *name, bool force)
{
    struct entry *e;
    int rc = -1;

    if ((e = zhash_lookup (attrs->hash, name))) {
        if ((e->flags & ATTR_IMMUTABLE)) {
            errno = EPERM;
            goto done;
        }
        zhash_delete (attrs->hash, name);
    }
    rc = 0;
done:
    return rc;
}

int attr_add (attr_t *attrs, const char *name, const char *val, int flags)
{
    struct entry *e;

    if (attrs == NULL || name == NULL) {
        errno = EINVAL;
        return -1;
    }
    if ((e = zhash_lookup (attrs->hash, name))) {
        errno = EEXIST;
        return -1;
    }
    if (!(e = entry_create (name, val, flags)))
        return -1;
    zhash_update (attrs->hash, name, e);
    zhash_freefn (attrs->hash, name, entry_destroy);
    return 0;
}

int attr_add_active (attr_t *attrs,
                     const char *name,
                     int flags,
                     attr_get_f get,
                     attr_set_f set,
                     void *arg)
{
    struct entry *e;
    int rc = -1;

    if (!attrs) {
        errno = EINVAL;
        goto done;
    }
    if ((e = zhash_lookup (attrs->hash, name))) {
        if (!set) {
            errno = EEXIST;
            goto done;
        }
        if (set (name, e->val, arg) < 0)
            goto done;
    }
    if (!(e = entry_create (name, NULL, flags)))
        goto done;
    e->set = set;
    e->get = get;
    e->arg = arg;
    zhash_update (attrs->hash, name, e);
    zhash_freefn (attrs->hash, name, entry_destroy);
    rc = 0;
done:
    return rc;
}

int attr_get (attr_t *attrs, const char *name, const char **val, int *flags)
{
    struct entry *e;
    int rc = -1;

    if (!attrs || !name) {
        errno = EINVAL;
        goto done;
    }
    if (!(e = zhash_lookup (attrs->hash, name))) {
        errno = ENOENT;
        goto done;
    }
    if (e->get) {
        if (!e->val || !(e->flags & ATTR_IMMUTABLE)) {
            const char *tmp;
            if (e->get (name, &tmp, e->arg) < 0)
                goto done;
            free (e->val);
            if (tmp) {
                if (!(e->val = strdup (tmp)))
                    goto done;
            }
            else
                e->val = NULL;
        }
    }
    if (val)
        *val = e->val;
    if (flags)
        *flags = e->flags;
    rc = 0;
done:
    return rc;
}

int attr_set (attr_t *attrs, const char *name, const char *val)
{
    struct entry *e;
    int rc = -1;

    if (!(e = zhash_lookup (attrs->hash, name))) {
        errno = ENOENT;
        goto done;
    }
    if ((e->flags & ATTR_IMMUTABLE)) {
        errno = EPERM;
        goto done;
    }
    if (e->set) {
        if (e->set (name, val, e->arg) < 0)
            goto done;
    }
    free (e->val);
    if (val) {
        if (!(e->val = strdup (val)))
            goto done;
    }
    else
        e->val = NULL;
    rc = 0;
done:
    return rc;
}

int attr_set_flags (attr_t *attrs, const char *name, int flags)
{
    struct entry *e;
    int rc = -1;

    if (!(e = zhash_lookup (attrs->hash, name))) {
        errno = ENOENT;
        goto done;
    }
    e->flags = flags;
    rc = 0;
done:
    return rc;
}

static int get_int (const char *name, const char **val, void *arg)
{
    int *i = arg;
    static char s[32];

    if (snprintf (s, sizeof (s), "%d", *i) >= sizeof (s)) {
        errno = EOVERFLOW;
        return -1;
    }
    *val = s;
    return 0;
}

static int set_int (const char *name, const char *val, void *arg)
{
    int *i = arg;
    char *endptr;
    long n;

    if (!val) {
        errno = EINVAL;
        return -1;
    }
    errno = 0;
    n = strtol (val, &endptr, 0);
    if (errno != 0 || *endptr != '\0') {
        errno = EINVAL;
        return -1;
    }
    if (n <= INT_MIN || n >= INT_MAX) {
        errno = ERANGE;
        return -1;
    }
    *i = (int)n;
    return 0;
}

int attr_add_int (attr_t *attrs, const char *name, int val, int flags)
{
    char s[32];

    if (snprintf (s, sizeof (s), "%d", val) >= sizeof (s)) {
        errno = EOVERFLOW;
        return -1;
    }
    return attr_add (attrs, name, s, flags);
}

int attr_add_active_int (attr_t *attrs, const char *name, int *val, int flags)
{
    return attr_add_active (attrs, name, flags, get_int, set_int, val);
}

static int get_uint32 (const char *name, const char **val, void *arg)
{
    uint32_t *i = arg;
    static char s[32];

    if (snprintf (s, sizeof (s), "%" PRIu32, *i) >= sizeof (s)) {
        errno = EOVERFLOW;
        return -1;
    }
    *val = s;
    return 0;
}

static int set_uint32 (const char *name, const char *val, void *arg)
{
    uint32_t *i = arg;
    char *endptr;
    unsigned long n;

    errno = 0;
    n = strtoul (val, &endptr, 0);
    if (errno != 0 || *endptr != '\0') {
        errno = EINVAL;
        return -1;
    }
    *i = n;
    return 0;
}

int attr_add_uint32 (attr_t *attrs, const char *name, uint32_t val, int flags)
{
    char val_string[32];

    snprintf (val_string, sizeof (val_string), "%"PRIu32, val);

    return attr_add (attrs, name, val_string, flags);
}

int attr_add_active_uint32 (attr_t *attrs,
                            const char *name,
                            uint32_t *val,
                            int flags)
{
    return attr_add_active (attrs, name, flags, get_uint32, set_uint32, val);
}

int attr_get_uint32 (attr_t *attrs, const char *name, uint32_t *value)
{
    const char *s;
    uint32_t i;
    char *endptr;

    if (attr_get (attrs, name, &s, NULL) < 0)
        return -1;

    errno = 0;
    i = strtoul (s, &endptr, 10);
    if (errno != 0 || *endptr != '\0') {
        errno = EINVAL;
        return -1;
    }
    *value = i;
    return 0;
}

const char *attr_first (attr_t *attrs)
{
    struct entry *e = zhash_first (attrs->hash);
    return e ? e->name : NULL;
}

const char *attr_next (attr_t *attrs)
{
    struct entry *e = zhash_next (attrs->hash);
    return e ? e->name : NULL;
}

int attr_cache_immutables (attr_t *attrs, flux_t *h)
{
    struct entry *e;

    e = zhash_first (attrs->hash);
    while (e) {
        if ((e->flags & ATTR_IMMUTABLE)) {
            if (flux_attr_set_cacheonly (h, e->name, e->val) < 0)
                return -1;
        }
        e = zhash_next (attrs->hash);
    }
    return 0;
}

/**
 ** Service
 **/

void getattr_request_cb (flux_t *h,
                         flux_msg_handler_t *mh,
                         const flux_msg_t *msg,
                         void *arg)
{
    attr_t *attrs = arg;
    const char *name;
    const char *val;
    int flags;

    if (flux_request_unpack (msg, NULL, "{s:s}", "name", &name) < 0)
        goto error;
    if (attr_get (attrs, name, &val, &flags) < 0)
        goto error;
    if (!val) {
        errno = ENOENT;
        goto error;
    }
    if (flux_respond_pack (h,
                           msg,
                           "{s:s s:i}",
                           "value", val,
                           "flags", flags) < 0)
        FLUX_LOG_ERROR (h);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        FLUX_LOG_ERROR (h);
}

void setattr_request_cb (flux_t *h,
                         flux_msg_handler_t *mh,
                         const flux_msg_t *msg,
                         void *arg)
{
    attr_t *attrs = arg;
    const char *name;
    const char *val;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s:s}",
                             "name", &name,
                             "value", &val) < 0)
        goto error;
    if (attr_set (attrs, name, val) < 0) {
        if (errno != ENOENT)
            goto error;
        if (attr_add (attrs, name, val, 0) < 0)
            goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        FLUX_LOG_ERROR (h);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        FLUX_LOG_ERROR (h);
}

void rmattr_request_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    attr_t *attrs = arg;
    const char *name;

    if (flux_request_unpack (msg, NULL, "{s:s}", "name", &name) < 0)
        goto error;
    if (attr_delete (attrs, name, false) < 0)
        goto error;
    if (flux_respond (h, msg, NULL) < 0)
        FLUX_LOG_ERROR (h);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        FLUX_LOG_ERROR (h);
}

void lsattr_request_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    attr_t *attrs = arg;
    const char *name;
    json_t *names = NULL, *js;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (!(names = json_array ())) {
        errno = ENOMEM;
        goto error;
    }
    name = attr_first (attrs);
    while (name) {
        if (!(js = json_string (name)))
            goto nomem;
        if (json_array_append_new (names, js) < 0) {
            json_decref (js);
            goto nomem;
        }
        name = attr_next (attrs);
    }
    if (flux_respond_pack (h, msg, "{s:O}", "names", names) < 0)
        FLUX_LOG_ERROR (h);
    json_decref (names);
    return;
nomem:
    errno = ENOMEM;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        FLUX_LOG_ERROR (h);
    json_decref (names);
}

/**
 ** Initialization
 **/

static const struct flux_msg_handler_spec handlers[] = {
    { FLUX_MSGTYPE_REQUEST, "attr.get",    getattr_request_cb, FLUX_ROLE_ALL },
    { FLUX_MSGTYPE_REQUEST, "attr.list",   lsattr_request_cb, FLUX_ROLE_ALL },
    { FLUX_MSGTYPE_REQUEST, "attr.set",    setattr_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "attr.rm",     rmattr_request_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};


int attr_register_handlers (attr_t *attrs, flux_t *h)
{
    if (flux_msg_handler_addvec (h, handlers, attrs, &attrs->handlers) < 0)
        return -1;
    return 0;
}

attr_t *attr_create (void)
{
    attr_t *attrs;

    if (!(attrs = calloc (1, sizeof (*attrs))))
        return NULL;
    if (!(attrs->hash = zhash_new ())) {
        attr_destroy (attrs);
        errno = ENOMEM;
        return NULL;
    }
    return attrs;
}

void attr_destroy (attr_t *attrs)
{
    if (attrs) {
        int saved_errno = errno;
        flux_msg_handler_delvec (attrs->handlers);
        zhash_destroy (&attrs->hash);
        free (attrs);
        errno = saved_errno;

    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
