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
#include <errno.h>
#include <string.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libtestutil/util.h"

struct entry {
    char *key;
    char *val;
    int flags;
};

// FLUX_ATTRFLAG_IMMUTABLE = 1
static struct entry hardwired[] = {
    { .key = "cow",     .val = "moo",   .flags = 1 },
    { .key = "duck",    .val = "quack", .flags = 1 },
    { .key = "chick",   .val = "peep",  .flags = 1  },
    { .key = "fox",     .val = "-",     .flags = 1  },
    { .key = "bear",    .val = "roar",  .flags = 1  },
    { .key = "hostlist", .val = "foo[0-2]", .flags = 1 },
    { .key = "broker.starttime", .val = "3.14", .flags = 1 },
    { .key = NULL,      .val = NULL,    .flags = 1  },
};

static bool lookup_hardwired (const char *key, const char **val, int *flags)
{
    int i;
    for (i = 0; hardwired[i].key != NULL; i++) {
        if (!strcmp (hardwired[i].key, key)) {
            if (val)
                *val = hardwired[i].val;
            if (flags)
                *flags = hardwired[i].flags;
            return true;
        }
    }
    return false;
}

static volatile int get_count = 0;
void get_cb (flux_t *h, flux_msg_handler_t *mh,
             const flux_msg_t *msg, void *arg)
{
    zhashx_t *attrs = arg;
    const char *name;
    const char *value;
    int flags = 0;
    get_count++;
    if (flux_request_unpack (msg, NULL, "{s:s}", "name", &name) < 0)
        goto error;
    if (!lookup_hardwired (name, &value, &flags)
                            && !(value = zhashx_lookup (attrs, name))) {
        errno = ENOENT;
        goto error;
    }
    diag ("attr.get: %s=%s (flags=%d)", name, value, flags);
    if (flux_respond_pack (h, msg, "{s:s s:i}", "value", value,
                                                "flags", flags) < 0)
        BAIL_OUT ("flux_respond failed");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        BAIL_OUT ("flux_respond_error failed");
}

void set_cb (flux_t *h, flux_msg_handler_t *mh,
             const flux_msg_t *msg, void *arg)
{
    zhashx_t *attrs = arg;
    const char *name;
    const char *value;
    if (flux_request_unpack (msg, NULL, "{s:s s:s}", "name", &name,
                                                     "value", &value) < 0)
        goto error;
    if (lookup_hardwired (name, NULL, NULL)) {
        errno = EPERM;
        goto error;
    }
    diag ("attr.set: %s=%s", name, value);
    zhashx_update (attrs, name, (char *)value);
    if (flux_respond (h, msg, NULL) < 0)
        BAIL_OUT ("flux_respond failed");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        BAIL_OUT ("flux_respond_error failed");
}

void rm_cb (flux_t *h, flux_msg_handler_t *mh,
            const flux_msg_t *msg, void *arg)
{
    zhashx_t *attrs = arg;
    const char *name;
    if (flux_request_unpack (msg, NULL, "{s:s}", "name", &name) < 0)
        goto error;
    if (lookup_hardwired (name, NULL, NULL)) {
        errno = EPERM;
        goto error;
    }
    if (!zhashx_lookup (attrs, name)) {
        errno = ENOENT;
        goto error;
    }
    diag ("attr.rm: %s", name);
    zhashx_delete (attrs, name);
    if (flux_respond (h, msg, NULL) < 0)
        BAIL_OUT ("flux_respond failed");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        BAIL_OUT ("flux_respond_error failed");
}

static const struct flux_msg_handler_spec tab[] = {
    { FLUX_MSGTYPE_REQUEST, "attr.get",    get_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "attr.set",    set_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "attr.rm",     rm_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

static void *valdup (const void *item)
{
    return strdup (item);
}
static void valfree (void **item)
{
    if (item) {
        free (*item);
        *item = NULL;
    }
}

int test_server (flux_t *h, void *arg)
{
    flux_msg_handler_t **handlers;
    zhashx_t *attrs;

    if (!(attrs = zhashx_new ()))
        BAIL_OUT ("zhashx_new");
    zhashx_set_duplicator (attrs, valdup);
    zhashx_set_destructor (attrs, valfree);

    if (flux_msg_handler_addvec (h, tab, attrs, &handlers) < 0)
        BAIL_OUT ("flux_msg_handler_addvec");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        BAIL_OUT ("flux_reactor_run failed");

    flux_msg_handler_delvec (handlers);
    zhashx_destroy (&attrs);
    return 0;
}

int main (int argc, char *argv[])
{
    flux_t *h;
    const char *value;
    const char *value2;

    plan (NO_PLAN);

    test_server_environment_init ("attr-test");

    if (!(h = test_server_create (0, test_server, NULL)))
        BAIL_OUT ("test_server_create failed");

    /* get ENOENT */

    errno = 0;
    get_count = 0;
    ok (flux_attr_get (h, "notakey") == NULL && errno == ENOENT
        && get_count == 1,
        "flux_attr_get name=notakey fails with ENOENT (with rpc)");

    /* set, get */

    ok (flux_attr_set (h, "foo", "bar") == 0,
        "flux_attr_set foo=bar works");
    ok (flux_attr_set (h, "baz", "meep") == 0,
        "flux_attr_set baz=meep works");

    get_count = 0;
    value = flux_attr_get (h, "foo");
    ok (value && !strcmp (value, "bar") && get_count == 1,
        "flux_attr_get foo=bar (with rpc)");
    value = flux_attr_get (h, "foo");
    ok (value && !strcmp (value, "bar") && get_count == 2,
        "flux_attr_get foo=bar (with 2nd rpc)");

    get_count = 0;
    value2 = flux_attr_get (h, "baz");
    ok (value2 && !strcmp (value2, "meep") && get_count == 1,
        "flux_attr_get baz=meep (with rpc)");
    value2 = flux_attr_get (h, "baz");
    ok (value2 && !strcmp (value2, "meep") && get_count == 2,
        "flux_attr_get baz=meep (with 2nd rpc)");

    ok (value && !strcmp (value, "bar"),
        "const return value of flux_attr_get foo=bar still valid");

    /* get (cached) */

    get_count = 0;
    value = flux_attr_get (h, "cow");
    ok (value && !strcmp (value, "moo") && get_count == 1,
        "flux_attr_get cow=moo (with rpc)");
    get_count = 0;
    value = flux_attr_get (h, "chick");
    ok (value && !strcmp (value, "peep") && get_count == 1,
        "flux_attr_get chick=peep (with rpc)");
    get_count = 0;
    value = flux_attr_get (h, "cow");
    ok (value && !strcmp (value, "moo") && get_count == 0,
        "flux_attr_get cow=moo (cached)");
    get_count = 0;
    value = flux_attr_get (h, "chick");
    ok (value && !strcmp (value, "peep") && get_count == 0,
        "flux_attr_get chick=peep (cached)");

    /* cacheonly */

    ok (flux_attr_set_cacheonly (h, "fake", "42") == 0,
        "flux_attr_set_cacheonly fake=42");
    get_count = 0;
    value = flux_attr_get (h, "fake");
    ok (value && !strcmp (value, "42") && get_count == 0,
        "flux_attr_get fake=42 (no rpc)");

    ok (flux_attr_set_cacheonly (h, "fake", NULL) == 0,
        "flux_attr_set_cacheonly fake=NULL");
    get_count = 0;
    errno = 0;
    ok (flux_attr_get (h, "fake") == NULL && errno == ENOENT && get_count == 1,
        "flux_attr_get fake failed with ENOENT (with rpc)");

    /* set - invalid args */
    errno = 0;
    ok (flux_attr_set (NULL, "foo", "bar") < 0 && errno == EINVAL,
        "flux_attr_set h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_attr_set (h, NULL, "bar") < 0 && errno == EINVAL,
        "flux_attr_set name=NULL fails with EINVAL");

    /* get - invalid args */
    errno = 0;
    ok (flux_attr_get (NULL, "foo") == NULL && errno == EINVAL,
        "flux_attr_get h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_attr_get (h, NULL) == NULL && errno == EINVAL,
        "flux_attr_get name=NULL fails with EINVAL");

    /* cacheonly - invalid args */
    errno = 0;
    ok (flux_attr_set_cacheonly (NULL, "foo", "bar") < 0 && errno == EINVAL,
        "flux_attr_set_cacheonly h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_attr_set_cacheonly (h, NULL, "bar") < 0 && errno == EINVAL,
        "flux_attr_set_cacheonly name=NULL fails with EINVAL");

    /* test flux_get_hostbyrank () */
    ok ((value = flux_get_hostbyrank (NULL, 42)) != NULL
        && !strcmp (value, "(null)"),
        "flux_get_hostbyrank h=NULL returns (null)");
    ok ((value = flux_get_hostbyrank (h, FLUX_NODEID_ANY)) != NULL
        && !strcmp (value, "any"),
        "flux_get_hostbyrank FLUX_NODEID_ANY returns any");
    ok ((value = flux_get_hostbyrank (h, FLUX_NODEID_UPSTREAM)) != NULL
        && !strcmp (value, "upstream"),
        "flux_get_hostbyrank FLUX_NODEID_UPSTREAMreturns upstream");
    ok ((value = flux_get_hostbyrank (h, 2)) != NULL
        && !strcmp (value, "foo2"),
        "flux_get_hostbyrank 2 returns foo2");
    ok ((value = flux_get_hostbyrank (h, 3)) != NULL
        && !strcmp (value, "(null)"),
        "flux_get_hostbyrank 3 returns (null)");

    /* test flux_get_rankbyhost () */
    errno = 0;
    ok (flux_get_rankbyhost (NULL, "foo2") < 0 && errno == EINVAL,
        "flux_get_rankbyhost h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_get_rankbyhost (h, NULL) < 0 && errno == EINVAL,
        "flux_get_rankbyhost host=NULL fails with EINVAL");
    errno = 0;
    ok (flux_get_rankbyhost (h, "foo3") < 0 && errno == ENOENT,
        "flux_get_rankbyhost host=foo3 fails with ENOENT");
    ok (flux_get_rankbyhost (h, "foo2") == 2,
        "flux_get_rankbyhost host=foo2 returns 2");

    /* test flux_hostmap_lookup () */
    flux_error_t error;
    errno = 0;
    ok (flux_hostmap_lookup (NULL, "foo", NULL) == NULL && errno == EINVAL,
        "flux_hostmap_lookup h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_hostmap_lookup (h, NULL, &error) == NULL && errno == EINVAL,
        "flux_hostmap_lookup targets=NULL fails with EINVAL");
    errno = 0;
    ok (flux_hostmap_lookup (h, "foo3", &error) == NULL && errno == ENOENT,
        "flux_hostmap_lookup targets=foo3 fails with ENOENT");
    is (error.text, "host foo3 not found in host map",
        "error.text is as expected");

    ok (flux_hostmap_lookup (h, "3", &error) == NULL && errno == ENOENT,
        "flux_hostmap_lookup targets=3 fails with ENOENT");
    is (error.text, "rank 3 is not in host map",
        "error.text is as expected");

    ok (flux_hostmap_lookup (h, "foo[", &error) == NULL && errno == EINVAL,
        "flux_hostmap_lookup targets=foo[ fails with EINVAL");
    is (error.text, "target must be a valid idset or hostlist",
        "error.text is as expected");

    char *s = flux_hostmap_lookup (h, "foo2", &error);
    ok (s != NULL,
        "flux_hostmap_lookup targets=foo2 returns %s", s);
    is (s, "2",
        "value is expected");
    free (s);

    s = flux_hostmap_lookup (h, "1-2", &error);
    ok (s != NULL,
        "flux_hostmap_lookup targets=1-2 returns %s", s);
    is (s, "foo[1-2]",
        "value is expected");
    free (s);

    /* test flux_get_instance_starttime */
    double d;
    ok (flux_get_instance_starttime (h, &d) == 0 && d == 3.14,
        "flux_get_instance_starttime works");
    errno = 0;
    ok (flux_get_instance_starttime (NULL, &d) < 0 && errno == EINVAL,
        "flux_get_instance_starttime h=NULL fails with EINVAL");

    test_server_stop (h);
    flux_close (h);

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

