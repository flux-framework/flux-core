/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* boot_flub.c - FLUx Boot protocol
 *
 * Add a broker to an existing Flux instance.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libidset/idset.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/ipaddr.h"
#include "ccan/str/str.h"

#include "attr.h"
#include "overlay.h"
#include "topology.h"

#include "boot_flub.h"

struct boot_info {
    int size;
    int rank;
    json_t *attrs;
};

struct boot_parent {
    char *pubkey;
    int rank;
    const char *uri;
};

static int wait_for_group_membership (flux_t *h,
                                      const char *name,
                                      int rank,
                                      flux_error_t *error)
{
    flux_future_t *f;
    bool online = false;

    if (!(f = flux_rpc_pack (h,
                             "groups.get",
                             0,
                             FLUX_RPC_STREAMING,
                             "{s:s}",
                             "name", "broker.online"))) {
        errprintf (error, "broker.online: %s", strerror (errno));
        return -1;
    }
    do {
        const char *members;
        struct idset *ids;

        if (flux_rpc_get_unpack (f, "{s:s}", "members", &members) < 0) {
            errprintf (error, "broker.online: %s", future_strerror (f, errno));
            break;
        }
        if ((ids = idset_decode (members))
            && idset_test (ids, rank))
            online = true;
        idset_destroy (ids);
        flux_future_reset (f);
    } while (!online);
    flux_future_destroy (f);

    return online ? 0 : -1;
}

static int set_attrs (attr_t *attrs, json_t *dict)
{
    const char *key;
    json_t *val;

    json_object_foreach (dict, key, val) {
        const char *s = json_string_value (val);
        if (!s) {
            errno = EPROTO;
            return -1;
        }
        if (attr_add (attrs, key, s, ATTR_IMMUTABLE) < 0)
            return -1;
    }
    return 0;
}

int boot_flub (struct broker *ctx, flux_error_t *error)
{
    const char *uri = NULL;
    flux_t *h;
    flux_future_t *f = NULL;
    flux_future_t *f2 = NULL;
    struct topology *topo = NULL;
    const char *topo_uri;
    struct boot_info info;
    struct boot_parent parent;
    const char *bind_uri = NULL;
    int rc = -1;

    /* Ask a Flux instance to allocate an available rank.
     * N.B. the broker unsets FLUX_URI so either it is set on the
     * command line via broker.boot-server, or the compiled-in path
     * is used (system instance).
     */
    (void)attr_get (ctx->attrs, "broker.boot-server", &uri, NULL);
    if (!(h = flux_open_ex (uri, 0, error)))
        return -1;
    if (!(f = flux_rpc_pack (h,
                             "overlay.flub-getinfo",
                             0,
                             0,
                             "{}"))
        || flux_rpc_get_unpack (f,
                                "{s:i s:i s:o}",
                                "rank", &info.rank,
                                "size", &info.size,
                                "attrs", &info.attrs) < 0) {
        errprintf (error, "%s", future_strerror (f, errno));
        goto out;
    }
    /* Set instance attributes obtained from boot server.
     */
    if (set_attrs (ctx->attrs, info.attrs) < 0) {
        errprintf (error, "error setting attributes: %s", strerror (errno));
        goto out;
    }
    /* Create topology.  All ranks are assumed to have the same topology.
     * The tbon.topo attribute is set in overlay_create() if not provided
     * on the command line.
     */
    if (attr_get (ctx->attrs, "tbon.topo", &topo_uri, NULL) < 0) {
        errprintf (error, "error fetching tbon.topo attribute");
        goto out;
    }
    if (!(topo = topology_create (topo_uri, info.size, NULL))
        || topology_set_rank (topo, info.rank) < 0
        || overlay_set_topology (ctx->overlay, topo) < 0) {
        errprintf (error, "error creating topology: %s", strerror (errno));
        goto out;
    }
    if ((parent.rank = topology_get_parent (topo)) < 0) {
        errprintf (error,
                   "rank %d has no parent in %s topology",
                   info.rank,
                   topo_uri);
        goto out;
    }
    /* Exchange public keys with TBON parent and obtain its URI.
     */
    if (wait_for_group_membership (h, "broker.online", parent.rank, error) < 0)
        goto out;
    if (!(f2 = flux_rpc_pack (h,
                              "overlay.flub-kex",
                              parent.rank,
                              0,
                              "{s:s s:s}",
                              "name", overlay_cert_name (ctx->overlay),
                              "pubkey", overlay_cert_pubkey (ctx->overlay)))
        || flux_rpc_get_unpack (f2,
                                "{s:s s:s}",
                                "pubkey", &parent.pubkey,
                                "uri", &parent.uri) < 0) {
        errprintf (error, "%s", future_strerror (f, errno));
        goto out;
    }
    /* Inform overlay subsystem of parent info.
     */
    if (overlay_set_parent_uri (ctx->overlay, parent.uri) < 0
        || overlay_set_parent_pubkey (ctx->overlay, parent.pubkey) < 0) {
        errprintf (error,
                   "error setting up overlay parameters: %s",
                   strerror (errno));
        goto out;
    }
    /* If there are children, bind to zmq socket and update tbon.endpoint.
     * Since we don't know if OUR children are co-located on the same node,
     * always use the tcp transport.
     */
    if (topology_get_child_ranks (topo, NULL, 0) > 0) {
        char ipaddr[HOST_NAME_MAX + 1];
        char *wild = NULL;

        overlay_set_ipv6 (ctx->overlay, 1);
        if (ipaddr_getprimary (ipaddr,
                               sizeof (ipaddr),
                               error->text,
                               sizeof (error->text)) < 0)
            goto out;
        if (asprintf (&wild, "tcp://%s:*", ipaddr) < 0
            || overlay_bind (ctx->overlay, wild) < 0) {
            errprintf (error, "error binding to tcp://%s:*", ipaddr);
            ERRNO_SAFE_WRAP (free, wild);
            goto out;
        }
        bind_uri = overlay_get_bind_uri (ctx->overlay);
    }
    if (attr_add (ctx->attrs, "tbon.endpoint", bind_uri, ATTR_IMMUTABLE) < 0) {
        errprintf (error, "setattr tbon.endpoint");
        goto out;
    }
    rc = 0;
out:
    topology_decref (topo);
    flux_future_destroy (f);
    flux_future_destroy (f2);
    flux_close (h);
    return rc;
}

// vi:ts=4 sw=4 expandtab
