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
#include <flux/core.h>

#include "ccan/str/str.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libhostlist/hostlist.h"
#include "src/common/libidset/idset.h"
#include "src/common/libutil/errprintf.h"

enum {
    FLUX_ATTRFLAG_IMMUTABLE = 1,
};

struct attr_cache {
    zhashx_t *cache;        // immutable values
    zhashx_t *temp;         // values that stay valid until next lookup

    zlistx_t *cache_iter;
    struct hostlist *hostlist;
};

static void attr_cache_destroy (struct attr_cache *c)
{
    if (c) {
        int saved_errno = errno;
        zlistx_destroy (&c->cache_iter);
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
    flux_future_t *f = NULL;
    char *cpy = NULL;
    const char *orig_name = name;
    char *proxy_remote = NULL;

    if (!h || !name) {
        errno = EINVAL;
        return NULL;
    }
    if (!(c = get_attr_cache (h)))
        return NULL;

    /*  The attribute parent-uri is treated specifically here, since
     *  a process connected to this instance via flux-proxy(1) should
     *  have the parent-uri returned as a usable remote URI.
     *
     *  Therefore, if FLUX_PROXY_REMOTE is set in the current environment,
     *  and the attribute is parent-uri, instead actually return the
     *  handle-specific parent-remote-uri attribute. If this is not yet
     *  available, fetch parent-uri and construct the remote uri by
     *  substituting FLUX_PROXY_REMOTE in an ssh:// uri.
     */
    if (streq (name, "parent-uri")
        && (proxy_remote = getenv ("FLUX_PROXY_REMOTE")))
        name = "parent-remote-uri";
    if ((val = zhashx_lookup (c->cache, name)))
        return val;

    /*  If FLUX_PROXY_REMOTE was set, try a lookup of 'orig_name' in
     *  the cache before attempting an RPC. If successful, then set
     *  the immutable flag for the parent-remote-uri attr (it should not
     *  change), since 'flags' will not be set by the RPC, which is being
     *  skipped.
     */
    if (proxy_remote && (val = zhashx_lookup (c->cache, orig_name)))
        flags = FLUX_ATTRFLAG_IMMUTABLE;

    /*  If we still don't have a value for this attribute, try an RPC:
     */
    if (!val) {
        if (!(f = flux_rpc_pack (h,
                                 "attr.get",
                                 FLUX_NODEID_ANY,
                                 0,
                                 "{s:s}",
                                 "name", orig_name)))
            return NULL;
        if (flux_rpc_get_unpack (f,
                                 "{s:s s:i}",
                                 "value", &val,
                                 "flags", &flags) < 0)
            goto done;
    }

    /*  If proxy_remote is non-NULL then parent-uri has been aliased to
     *  parent-remote-uri. Swap a local URI to a remote:
     */
    if (proxy_remote
        && strstarts (val, "local://")) {
        if (asprintf (&cpy, "ssh://%s%s", proxy_remote, val+8) < 0)
            goto done;
    }
    else if (!(cpy = strdup (val)))
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

    if (!h || !name || !val) {
        errno = EINVAL;
        return -1;
    }
    f = flux_rpc_pack (h,
                       "attr.set",
                       FLUX_NODEID_ANY,
                       0,
                       "{s:s s:s}",
                       "name", name,
                       "value", val);
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

const char *flux_attr_cache_first (flux_t *h)
{
    struct attr_cache *c;

    if (!h || !(c = get_attr_cache (h)))
        return NULL;
    zlistx_destroy (&c->cache_iter);
    if (!(c->cache_iter = zhashx_keys (c->cache)))
        return NULL;
    return zlistx_first (c->cache_iter);
}

const char *flux_attr_cache_next (flux_t *h)
{
    struct attr_cache *c;

    if (!h || !(c = get_attr_cache (h)))
        return NULL;
    if (!c->cache_iter)
        return NULL;
    return zlistx_next (c->cache_iter);
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

static struct hostlist * get_hostlist (flux_t *h)
{
    struct attr_cache *c;

    if (!(c = get_attr_cache (h)))
        return NULL;
    if (!c->hostlist) {
        const char *val;
        if (!(val = flux_attr_get (h, "hostlist"))
            || !(c->hostlist = hostlist_decode (val)))
            return NULL;
    }
    return c->hostlist;
}

const char *flux_get_hostbyrank (flux_t *h, uint32_t rank)
{
    struct hostlist *hl;
    const char *result;

    if (rank == FLUX_NODEID_ANY)
        return "any";
    if (rank == FLUX_NODEID_UPSTREAM)
        return "upstream";
    if (!(hl = get_hostlist (h)))
        goto error;
    if (!(result = hostlist_nth (hl, rank)))
        goto error;
    return result;
error:
    return "(null)";
}

int flux_get_rankbyhost (flux_t *h, const char *host)
{
    struct hostlist *hl;

    if (!(hl = get_hostlist (h)))
        return -1;
    return hostlist_find (hl, host);
}

char *flux_hostmap_lookup (flux_t *h,
                           const char *targets,
                           flux_error_t *errp)
{
    struct hostlist *hostmap;
    struct hostlist *hosts = NULL;
    struct idset *ranks = NULL;
    char *s = NULL;

    if (!(hostmap = get_hostlist (h))) {
        errprintf (errp, "%s", strerror (errno));
        return NULL;
    }

    if ((ranks = idset_decode (targets))) {
        unsigned int rank;
        if (!(hosts = hostlist_create ())) {
            errprintf (errp, "Out of memory");
            goto err;
        }
        rank = idset_first (ranks);
        while (rank != IDSET_INVALID_ID) {
            const char *host;
            if (!(host = hostlist_nth (hostmap, rank))) {
                errprintf (errp, "rank %u is not in host map", rank);
                goto err;
            }
            if (hostlist_append (hosts, host) < 0) {
                errprintf (errp,
                           "failed appending host %s: %s",
                           host,
                           strerror (errno));
                goto err;
            }
            rank = idset_next (ranks, rank);
        }
        if (!(s = hostlist_encode (hosts))) {
            errprintf (errp,
                       "hostlist_encode: %s",
                       strerror (errno));
            goto err;
        }
    }
    else if ((hosts = hostlist_decode (targets))) {
        const char *host;
        int rank = 0;
        if (!(ranks = idset_create (0, IDSET_FLAG_AUTOGROW))) {
            errprintf (errp, "Out of memory");
            goto err;
        }
        host = hostlist_first (hosts);
        while (host) {
            if ((rank = hostlist_find (hostmap, host)) < 0) {
                errprintf (errp, "host %s not found in host map", host);
                goto err;
            }
            if (idset_set (ranks, rank) < 0) {
                errprintf (errp,
                           "idset_set (rank=%d): %s",
                           rank,
                           strerror (errno));
                goto err;
            }
            host = hostlist_next (hosts);
        }
        if (!(s = idset_encode (ranks, IDSET_FLAG_RANGE))) {
            errprintf (errp,
                       "error encoding idset: %s",
                       strerror (errno));
            goto err;
        }
    }
    else {
        errprintf (errp, "target must be a valid idset or hostlist");
        goto err;
    }
err:
    hostlist_destroy (hosts);
    idset_destroy (ranks);
    return s;
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
