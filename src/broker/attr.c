/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <jansson.h>

#include "src/common/libutil/oom.h"
#include "src/common/libutil/xzmalloc.h"

#include "attr.h"

struct attr_struct {
    zhash_t *hash;
};

struct entry {
    char *name;
    char *val;
    int flags;
    attr_set_f set;
    attr_get_f get;
    void *arg;
};

static void entry_destroy (void *arg)
{
    struct entry *e = arg;
    if (e) {
        if (e->val)
            free (e->val);
        if (e->name)
            free (e->name);
        free (e);
    }
}

static struct entry *entry_create (const char *name, const char *val, int flags)
{
    struct entry *e = xzmalloc (sizeof (*e));
    if (name)
        e->name = xstrdup (name);
    if (val)
        e->val = xstrdup (val);
    e->flags = flags;
    return e;
}

int attr_delete (attr_t *attrs, const char *name, bool force)
{
    struct entry *e;
    int rc = -1;

    if ((e = zhash_lookup (attrs->hash, name))) {
        if ((e->flags & FLUX_ATTRFLAG_IMMUTABLE)) {
            errno = EPERM;
            goto done;
        }
        if (((e->flags & FLUX_ATTRFLAG_READONLY)
                            || (e->flags & FLUX_ATTRFLAG_ACTIVE)) && !force) {
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

    if (name == NULL || (flags & FLUX_ATTRFLAG_ACTIVE)) {
        errno = EINVAL;
        return -1;
    }
    if ((e = zhash_lookup (attrs->hash, name))) {
        errno = EEXIST;
        return -1;
    }
    e = entry_create (name, val, flags);
    zhash_update (attrs->hash, name, e);
    zhash_freefn (attrs->hash, name, entry_destroy);
    return 0;
}

int attr_add_active (attr_t *attrs, const char *name, int flags,
                        attr_get_f get, attr_set_f set, void *arg)
{
    struct entry *e;
    int rc = -1;

    if ((e = zhash_lookup (attrs->hash, name))) {
        if (!set) {
            errno = EEXIST;
            goto done;
        }
        if (set (name, e->val, arg) < 0)
            goto done;
    }
    e = entry_create (name, NULL, flags);
    e->set = set;
    e->get = get;
    e->arg = arg;
    e->flags |= FLUX_ATTRFLAG_ACTIVE;
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

    if (!(e = zhash_lookup (attrs->hash, name))) {
        errno = ENOENT;
        goto done;
    }
    if (e->get) {
        if (!e->val || !(e->flags & FLUX_ATTRFLAG_IMMUTABLE)) {
            const char *tmp;
            if (e->get (name, &tmp, e->arg) < 0)
                goto done;
            if (e->val)
                free (e->val);
            e->val = tmp ? xstrdup (tmp) : NULL;
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

int attr_set (attr_t *attrs, const char *name, const char *val, bool force)
{
    struct entry *e;
    int rc = -1;

    if (!(e = zhash_lookup (attrs->hash, name))) {
        errno = ENOENT;
        goto done;
    }
    if ((e->flags & FLUX_ATTRFLAG_IMMUTABLE)) {
        errno = EPERM;
        goto done;
    }
    if ((e->flags & FLUX_ATTRFLAG_READONLY) && !force) {
        errno = EPERM;
        goto done;
    }
    if (e->set) {
        if (e->set (name, val, e->arg) < 0)
            goto done;
    }
    if (e->val)
        free (e->val);
    e->val = val ? xstrdup (val) : NULL;
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
    int n = snprintf (s, sizeof (s), "%d", *i);

    assert (n <= sizeof (s));
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
    n = strtol (val, &endptr, 0);
    if (n <= INT_MIN || n >= INT_MAX) {
        errno = ERANGE;
        return -1;
    }
    if (*endptr != '\0') {
        errno = EINVAL;
        return -1;
    }
    *i = (int)n;
    return 0;
}

int attr_add_active_int (attr_t *attrs, const char *name, int *val, int flags)
{
    return attr_add_active (attrs, name, flags, get_int, set_int, val);
}

static int get_uint32 (const char *name, const char **val, void *arg)
{
    uint32_t *i = arg;
    static char s[32];
    int n = snprintf (s, sizeof (s), "%" PRIu32, *i);

    assert (n <= sizeof (s));
    *val = s;
    return 0;
}

static int set_uint32 (const char *name, const char *val, void *arg)
{
    uint32_t *i = arg;
    char *endptr;
    unsigned long n;

    n = strtoul (val, &endptr, 0);
    if (n == ULONG_MAX) /* ERANGE set by strtol */
        return -1;
    if (endptr == val || *endptr != '\0') {
        errno = EINVAL;
        return -1;
    }
    *i = n;
    return 0;
}

int attr_add_active_uint32 (attr_t *attrs, const char *name, uint32_t *val,
                            int flags)
{
    return attr_add_active (attrs, name, flags, get_uint32, set_uint32, val);
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

/**
 ** Service
 **/

void getattr_request_cb (flux_t *h, flux_msg_handler_t *w,
                         const flux_msg_t *msg, void *arg)
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
    if (flux_respond_pack (h, msg, "{s:s s:i}",
                                   "value", val,
                                   "flags", flags) < 0)
        FLUX_LOG_ERROR (h);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        FLUX_LOG_ERROR (h);
}

void setattr_request_cb (flux_t *h, flux_msg_handler_t *w,
                         const flux_msg_t *msg, void *arg)
{
    attr_t *attrs = arg;
    const char *name;
    const char *val;

    if (flux_request_unpack (msg, NULL, "{s:s s:s}",       /* SET */
                             "name", &name,
                             "value", &val) == 0) {
        if (attr_set (attrs, name, val, false) < 0) {
            if (errno != ENOENT)
                goto error;
            if (attr_add (attrs, name, val, 0) < 0)
                goto error;
        }
    }
    else if (flux_request_unpack (msg, NULL, "{s:s s:n}",  /* DELETE */
                                  "name", &name,
                                  "value") == 0) {
        if (attr_delete (attrs, name, false) < 0)
            goto error;
    }
    else
        goto error;
    if (flux_respond (h, msg, 0, NULL) < 0)
        FLUX_LOG_ERROR (h);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        FLUX_LOG_ERROR (h);
}

void lsattr_request_cb (flux_t *h, flux_msg_handler_t *w,
                        const flux_msg_t *msg, void *arg)
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
        if (!(js = json_string (name))) {
            errno = ENOMEM;
            goto error;
        }
        if (json_array_append_new (names, js) < 0) {
            json_decref (js);
            errno = ENOMEM;
            goto error;
        }
        name = attr_next (attrs);
    }
    if (flux_respond_pack (h, msg, "{s:O}", "names", names) < 0)
        FLUX_LOG_ERROR (h);
    json_decref (names);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        FLUX_LOG_ERROR (h);
    json_decref (names);
}

/**
 ** Initialization
 **/

static struct flux_msg_handler_spec handlers[] = {
    { FLUX_MSGTYPE_REQUEST, "attr.get",    getattr_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "attr.list",   lsattr_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "attr.set",    setattr_request_cb, 0, NULL },
    FLUX_MSGHANDLER_TABLE_END,
};


int attr_register_handlers (attr_t *attrs, flux_t *h)
{
    if (flux_msg_handler_addvec (h, handlers, attrs) < 0)
        return -1;
    /* allow any user to attr.get and attr.list */
    flux_msg_handler_allow_rolemask (handlers[0].w, FLUX_ROLE_ALL);
    flux_msg_handler_allow_rolemask (handlers[1].w, FLUX_ROLE_ALL);
    return 0;
}

void attr_unregister_handlers (void)
{
    flux_msg_handler_delvec (handlers);
}

attr_t *attr_create (void)
{
    attr_t *attrs = xzmalloc (sizeof (*attrs));
    if (!(attrs->hash = zhash_new ()))
        oom ();
    return attrs;
}

void attr_destroy (attr_t *attrs)
{
    if (attrs) {
        zhash_destroy (&attrs->hash);
        free (attrs);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
