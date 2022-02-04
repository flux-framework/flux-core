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
#include <stdbool.h>
#include <jansson.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libhostlist/hostlist.h"

#include "attr.h"
#include "rpc.h"

enum {
    FLUX_ATTRFLAG_IMMUTABLE = 1,
};

struct attr_cache {
    zhashx_t *cache;        // immutable values
    zhashx_t *temp;         // values that stay valid until next lookup

    struct hostlist *hostlist;
};

static void attr_cache_destroy (struct attr_cache *c)
{
    if (c) {
        int saved_errno = errno;
        zhashx_destroy (&c->cache);
        zhashx_destroy (&c->temp);
        hostlist_destroy (c->hostlist);
        free (c);
        errno = saved_errno;
    }
}

static void valfree (void **item)
{
    if (item) {
        free (*item);
        *item = NULL;
    }
}

static struct attr_cache *attr_cache_create (flux_t *h)
{
    struct attr_cache *c = calloc (1, sizeof (*c));
    if (!c)
        return NULL;
    if (!(c->cache = zhashx_new ()))
        goto nomem;
    zhashx_set_destructor (c->cache, valfree);
    if (!(c->temp = zhashx_new ()))
        goto nomem;
    zhashx_set_destructor (c->temp, valfree);
    return c;
nomem:
    errno = ENOMEM;
    attr_cache_destroy (c);
    return NULL;
}

static struct attr_cache *get_attr_cache (flux_t *h)
{
    const char *auxkey = "flux::attr_cache";
    struct attr_cache *c = flux_aux_get (h, auxkey);
    if (!c) {
        if (!(c = attr_cache_create (h)))
            return NULL;
        if (flux_aux_set (h, auxkey, c, (flux_free_f)attr_cache_destroy) < 0) {
            attr_cache_destroy (c);
            return NULL;
        }
    }
    return c;
}

const char *flux_attr_get (flux_t *h, const char *name)
{
    struct attr_cache *c;
    const char *val;
    int flags;
    flux_future_t *f;
    char *cpy = NULL;

    if (!h || !name) {
        errno = EINVAL;
        return NULL;
    }
    if (!(c = get_attr_cache (h)))
        return NULL;
    if ((val = zhashx_lookup (c->cache, name)))
        return val;
    if (!(f = flux_rpc_pack (h, "attr.get", FLUX_NODEID_ANY, 0, "{s:s}",
                                                                "name", name)))
        return NULL;
    if (flux_rpc_get_unpack (f, "{s:s s:i}", "value", &val,
                                             "flags", &flags) < 0)
        goto done;
    if (!(cpy = strdup (val)))
        goto done;
    if ((flags & FLUX_ATTRFLAG_IMMUTABLE))
        zhashx_update (c->cache, name, cpy);
    else
        zhashx_update (c->temp, name, cpy);
done:
    flux_future_destroy (f);
    return cpy;
}

int flux_attr_set (flux_t *h, const char *name, const char *val)
{
    flux_future_t *f;

    if (!h || !name) {
        errno = EINVAL;
        return -1;
    }
    if (val)
        f = flux_rpc_pack (h, "attr.set", FLUX_NODEID_ANY, 0, "{s:s s:s}",
                                                              "name", name,
                                                              "value", val);
    else
        f = flux_rpc_pack (h, "attr.rm", FLUX_NODEID_ANY, 0, "{s:s}",
                                                             "name", name);
    if (!f)
        return -1;
    if (flux_future_get (f, NULL) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    /* N.B. No cache update is necessary.
     * If immutable, the RPC will fail.
     * If not immutable, we have to look it up on next access anyway.
     */
    flux_future_destroy (f);
    return 0;
}

int flux_attr_set_cacheonly (flux_t *h, const char *name, const char *val)
{
    struct attr_cache *c;

    if (!h || !name) {
        errno = EINVAL;
        return -1;
    }
    if (!(c = get_attr_cache (h)))
        return -1;
    if (val) {
        char *cpy;
        if (!(cpy = strdup (val)))
            return -1;
        zhashx_update (c->cache, name, cpy);
    }
    else
        zhashx_delete (c->cache, name);
    return 0;
}

int flux_get_size (flux_t *h, uint32_t *size)
{
    const char *val;

    if (!(val = flux_attr_get (h, "size")))
        return -1;
    *size = strtoul (val, NULL, 10);
    return 0;
}

int flux_get_rank (flux_t *h, uint32_t *rank)
{
    const char *val;

    if (!(val = flux_attr_get (h, "rank")))
        return -1;
    *rank = strtoul (val, NULL, 10);
    return 0;
}

const char *flux_get_hostbyrank (flux_t *h, uint32_t rank)
{
    struct attr_cache *c;
    const char *result;

    if (rank == FLUX_NODEID_ANY)
        return "any";
    if (rank == FLUX_NODEID_UPSTREAM)
        return "upstream";
    if (!(c = get_attr_cache (h)))
        goto error;
    if (!c->hostlist) {
        const char *val;
        if (!(val = flux_attr_get (h, "hostlist"))
            || !(c->hostlist = hostlist_decode (val)))
            goto error;
    }
    if (!(result = hostlist_nth (c->hostlist, rank)))
        goto error;
    return result;
error:
    return "(null)";
}

int flux_get_rankbyhost (flux_t *h, const char *host)
{
    struct attr_cache *c;

    if (!(c = get_attr_cache (h)))
        return -1;
    if (!c->hostlist) {
        const char *val;
        if (!(val = flux_attr_get (h, "hostlist"))
            || !(c->hostlist = hostlist_decode (val)))
            return -1;
    }
    return hostlist_find (c->hostlist, host);
}

int flux_get_instance_starttime (flux_t *h, double *starttimep)
{
    flux_future_t *f;
    const char *attr = "broker.starttime";
    const char *s;
    double starttime;

    if (!(f = flux_rpc_pack (h, "attr.get", 0, 0, "{s:s}", "name", attr)))
        return -1;
    if (flux_rpc_get_unpack (f, "{s:s}", "value", &s) < 0)
        goto error;
    errno = 0;
    starttime = strtod (s, NULL);
    if (errno != 0)
        goto error;
    flux_future_destroy (f);
    if (starttimep)
        *starttimep = starttime;
    return 0;
error:
    flux_future_destroy (f);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
