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
#include <fnmatch.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/array_size/array_size.h"

#include "attr.h"

/* Flags description:
 *
 * ATTR_IMMUTABLE
 *   Once set, this attribute never changes and thus may be cached by
 *   clients. For example, 'rank' never changes once it is set, so there
 *   is no need to make an RPC to the broker to fetch it ever again once
 *   it is cached.
 *
 * ATTR_READONLY
 *   This attribute may NOT be set on the command line.  This flag
 *   does not imply ATTR_IMMUTABLE, nor anything about runtime updatability.
 *
 * ATTR_RUNTIME
 *   This attribute may be updated at runtime.  However, even without
 *   this flag, a runtime update may be forced.  The force flag permits
 *   the owner of an attribute to update it without extending this
 *   capability to users.  Again, the absence of this flag only imposes
 *   an advisory limitation, not a mandatory one.
 *
 * ATTR_CONFIG
 *   This attribute overrides a TOML configuration attribute of the same
 *   name when set on the command line.  The flag is not currently used.
 *
 * ATTR_READONLY, ATTR_RUNTIME, and ATTR_CONFIG match the labels associated
 * with attributes in the flux-broker-attributes(7) manual page.
 * Only ATTR_IMMUTABLE is exposed by the attr.get RPC so that clients can
 * determine whether or not an attribute should be cached.
 */

enum {
    ATTR_IMMUTABLE      = 0x01, // value never changes once set
    ATTR_READONLY       = 0x02, // value is not to be set on cmdline by users
    ATTR_RUNTIME        = 0x04, // value may be updated by users
    ATTR_CONFIG         = 0x08, // value overrides TOML config [unused]
};

struct registered_attr {
    const char *name;
    int flags;
};

struct broker_attr {
    zhashx_t *hash;
    flux_msg_handler_t **handlers;
};

struct entry {
    char *name;
    char *val;
    int flags;
    void *arg;
};

static struct registered_attr attrtab[] = {

    // general
    { "rank", ATTR_READONLY | ATTR_IMMUTABLE },
    { "size", ATTR_READONLY | ATTR_IMMUTABLE },
    { "version", ATTR_READONLY },
    { "rundir", 0 },
    { "rundir-cleanup", ATTR_IMMUTABLE },
    { "statedir", 0 },
    { "statedir-cleanup", ATTR_IMMUTABLE },
    { "security.owner", ATTR_READONLY | ATTR_IMMUTABLE },
    { "local-uri", ATTR_IMMUTABLE },
    { "parent-uri", ATTR_READONLY | ATTR_IMMUTABLE },
    { "instance-level", ATTR_READONLY | ATTR_IMMUTABLE },
    { "jobid", ATTR_READONLY | ATTR_IMMUTABLE },
    { "jobid-path", ATTR_READONLY | ATTR_IMMUTABLE },
    { "parent-kvs-namespace", ATTR_READONLY | ATTR_IMMUTABLE },
    { "hostlist", ATTR_IMMUTABLE },
    { "hostname", ATTR_IMMUTABLE },
    { "broker.mapping", ATTR_READONLY | ATTR_IMMUTABLE },
    { "broker.critical-ranks", ATTR_IMMUTABLE },
    { "broker.boot-method", ATTR_IMMUTABLE },
    { "broker.pid", ATTR_READONLY | ATTR_IMMUTABLE },
    { "broker.quorum", ATTR_IMMUTABLE },
    { "broker.quorum-warn", ATTR_IMMUTABLE },
    { "broker.shutdown-warn", ATTR_IMMUTABLE },
    { "broker.shutdown-timeout", ATTR_IMMUTABLE },
    { "broker.cleanup-timeout", 0 },
    { "broker.rc1_path", 0 },
    { "broker.rc3_path", 0 },
    { "broker.rc2_none", 0 },
    { "broker.rc2_pgrp", 0 },
    { "broker.exit-restart", ATTR_RUNTIME },
    { "broker.module-nopanic", ATTR_RUNTIME },
    { "broker.starttime", ATTR_READONLY | ATTR_IMMUTABLE },
    { "broker.sd-notify", 0 },
    { "broker.sd-stop-timeout", ATTR_IMMUTABLE },
    { "broker.exit-norestart", 0 },
    { "broker.recovery-mode", 0 },
    { "broker.uuid", ATTR_READONLY | ATTR_IMMUTABLE },
    { "conf.shell_initrc", ATTR_RUNTIME },
    { "conf.shell_pluginpath", ATTR_RUNTIME },
    { "config.path", ATTR_IMMUTABLE },

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
    { "tbon.interface-hint", ATTR_RUNTIME | ATTR_CONFIG | ATTR_IMMUTABLE },
    { "tbon.torpid_min", ATTR_CONFIG },
    { "tbon.torpid_max", ATTR_CONFIG },
    { "tbon.tcp_user_timeout", ATTR_CONFIG },
    { "tbon.connect_timeout", ATTR_CONFIG },

    // logging
    { "log-ring-size", ATTR_IMMUTABLE },
    { "log-forward-level", ATTR_IMMUTABLE },
    { "log-critical-level", ATTR_IMMUTABLE },
    { "log-filename", 0 },
    { "log-syslog-enable", ATTR_IMMUTABLE },
    { "log-syslog-level", ATTR_IMMUTABLE },
    { "log-stderr-mode", ATTR_IMMUTABLE },
    { "log-stderr-level", ATTR_RUNTIME },
    { "log-level", ATTR_IMMUTABLE },

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
    { "test-nr.*", 0 },
    { "test-im.*", ATTR_IMMUTABLE },

    // misc undocumented
    { "vendor.*", ATTR_RUNTIME | ATTR_IMMUTABLE }, // flux-pmix#109
    { "tbon.fanout", 0 }, // legacy, replaced by tbon.topo
};

static struct registered_attr *attrtab_lookup (const char *name)
{
    for (int i = 0; i < ARRAY_SIZE (attrtab); i++) {
        if (fnmatch (attrtab[i].name, name, 0) == 0)
            return &attrtab[i];
    }
    errno = ENOENT;
    return NULL;
}

static void entry_destroy (struct entry *e)
{
    if (e) {
        int saved_errno = errno;
        free (e->val);
        free (e->name);
        free (e);
        errno = saved_errno;
    }
}

static void entry_destructor (void **item)
{
    if (*item) {
        entry_destroy (*item);
        *item = NULL;
    }
}

static struct entry *entry_create (const char *name, const char *val, int flags)
{
    struct entry *e;
    if (!(e = calloc (1, sizeof (*e))))
        return NULL;

    if (!(e->name = strdup (name)))
        goto cleanup;
    if (val && !(e->val = strdup (val)))
        goto cleanup;
    e->flags = flags;
    return e;
cleanup:
    entry_destroy (e);
    return NULL;
}

int attr_delete (attr_t *attrs, const char *name)
{
    struct entry *e;
    int rc = -1;

    if ((e = zhashx_lookup (attrs->hash, name))) {
        if ((e->flags & ATTR_IMMUTABLE)) {
            errno = EPERM;
            goto done;
        }
        zhashx_delete (attrs->hash, name);
    }
    rc = 0;
done:
    return rc;
}

int attr_set_cmdline (attr_t *attrs,
                      const char *name,
                      const char *val,
                      flux_error_t *errp)
{
    struct registered_attr *reg;


    if (!attrs || !name) {
        errno = EINVAL;
        return errprintf (errp, "invalid argument");
    }
    if (!(reg = attrtab_lookup (name)))
        return errprintf (errp, "unknown attribute");
    if ((reg->flags & ATTR_READONLY)) {
        errno = EINVAL;
        return errprintf (errp, "attribute may not be set on the command line");
    }
    if (attr_set (attrs, name, val) < 0)
        return errprintf (errp, "%s", strerror (errno));
    return 0;
}

static struct entry *attr_get_entry (attr_t *attrs, const char *name)
{
    struct entry *e;

    if (!attrs || !name) {
        errno = EINVAL;
        return NULL;
    }
    if (!(e = zhashx_lookup (attrs->hash, name))) {
        errno = ENOENT;
        return NULL;
    }
    return e;
}

int attr_get (attr_t *attrs, const char *name, const char **val)
{
    struct entry *e;

    if (!(e = attr_get_entry (attrs, name)))
        return -1;
    if (val)
        *val = e->val;
    return 0;
}

int attr_set (attr_t *attrs, const char *name, const char *val)
{
    struct entry *e;

    if ((e = zhashx_lookup (attrs->hash, name))) {
        char *cpy = NULL;
        if ((e->flags & ATTR_IMMUTABLE)) {
            errno = EPERM;
            return -1;
        }
        if (val && !(cpy = strdup (val)))
            return -1;
        free (e->val);
        e->val = cpy;
    }
    else {
        struct registered_attr *reg;
        if (!(reg = attrtab_lookup (name)))
            return -1;
        if (!(e = entry_create (name, val, reg->flags)))
            return -1;
        if (zhashx_insert (attrs->hash, e->name, e) < 0) {
            errno = EEXIST;
            return -1;
        }
    }
    return 0;
}

const char *attr_first (attr_t *attrs)
{
    struct entry *e = zhashx_first (attrs->hash);
    return e ? e->name : NULL;
}

const char *attr_next (attr_t *attrs)
{
    struct entry *e = zhashx_next (attrs->hash);
    return e ? e->name : NULL;
}

int attr_cache_immutables (attr_t *attrs, flux_t *h)
{
    struct entry *e;

    e = zhashx_first (attrs->hash);
    while (e) {
        if ((e->flags & ATTR_IMMUTABLE)) {
            if (flux_attr_set_cacheonly (h, e->name, e->val) < 0)
                return -1;
        }
        e = zhashx_next (attrs->hash);
    }
    return 0;
}

/**
 ** Service
 **/

static void getattr_request_cb (flux_t *h,
                                flux_msg_handler_t *mh,
                                const flux_msg_t *msg,
                                void *arg)
{
    attr_t *attrs = arg;
    const char *name;
    struct entry *e;

    if (flux_request_unpack (msg, NULL, "{s:s}", "name", &name) < 0)
        goto error;
    if (!(e = attr_get_entry (attrs, name)))
        goto error;
    if (!e->val) {
        errno = ENOENT;
        goto error;
    }
    if (flux_respond_pack (h,
                           msg,
                           "{s:s s:i}",
                           "value", e->val,
                           "flags", (e->flags & ATTR_IMMUTABLE)) < 0)
        FLUX_LOG_ERROR (h);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        FLUX_LOG_ERROR (h);
}

static void setattr_request_cb (flux_t *h,
                                flux_msg_handler_t *mh,
                                const flux_msg_t *msg,
                                void *arg)
{
    attr_t *attrs = arg;
    const char *name;
    const char *val;
    int force = 0;
    struct registered_attr *reg;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s:s s?b}",
                             "name", &name,
                             "value", &val,
                             "force", &force) < 0)
        goto error;
    if (!(reg = attrtab_lookup (name))) {
        errmsg = "unknown attribute";
        goto error;
    }
    /* If name is found in the hash, then this is an update rather
     * than the initial value.  Don't allow updates without the
     * ATTR_RUNTIME flag, unless forced.
     */
    if (zhashx_lookup (attrs->hash, name)) {
        if (!force && !(reg->flags & ATTR_RUNTIME)) {
            errmsg = "attribute may not be updated";
            errno = EINVAL;
            goto error;
        }
    }
    if (attr_set (attrs, name, val) < 0)
        goto error;
    if (flux_respond (h, msg, NULL) < 0)
        FLUX_LOG_ERROR (h);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        FLUX_LOG_ERROR (h);
}

static void rmattr_request_cb (flux_t *h,
                               flux_msg_handler_t *mh,
                               const flux_msg_t *msg,
                               void *arg)
{
    attr_t *attrs = arg;
    const char *name;

    if (flux_request_unpack (msg, NULL, "{s:s}", "name", &name) < 0)
        goto error;
    if (attr_delete (attrs, name) < 0)
        goto error;
    if (flux_respond (h, msg, NULL) < 0)
        FLUX_LOG_ERROR (h);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        FLUX_LOG_ERROR (h);
}

static void lsattr_request_cb (flux_t *h,
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
    if (!(attrs->hash = zhashx_new ())) {
        attr_destroy (attrs);
        errno = ENOMEM;
        return NULL;
    }
    zhashx_set_key_destructor (attrs->hash, NULL);
    zhashx_set_key_duplicator (attrs->hash, NULL);
    zhashx_set_destructor (attrs->hash, entry_destructor);
    return attrs;
}

void attr_destroy (attr_t *attrs)
{
    if (attrs) {
        int saved_errno = errno;
        flux_msg_handler_delvec (attrs->handlers);
        zhashx_destroy (&attrs->hash);
        free (attrs);
        errno = saved_errno;

    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
