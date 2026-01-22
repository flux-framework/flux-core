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
#include <unistd.h>
#include <jansson.h>
#include <flux/taskmap.h>

#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/ipaddr.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libpmi/upmi.h"
#include "src/common/libpmi/bizcache.h"
#include "ccan/str/str.h"

#include "compat.h"
#include "overlay.h"
#include "topology.h"
#include "boot_util.h"
#include "boot_pmi.h"


/* Return the number of ranks[] members that are in the same clique as rank.
 */
static int clique_ranks (struct taskmap *map, int rank, int *ranks, int nranks)
{
    int count = 0;

    if (map) {
        int nid = taskmap_nodeid (map, rank);
        if (nid >= 0) {
            for (int i = 0; i < nranks; i++) {
                if (taskmap_nodeid (map, ranks[i]) == nid)
                    count++;
            }
        }
    }
    return count;
}

/* Check if TCP should be used, even if IPC could work.
 */
static bool get_prefer_tcp (flux_t *h)
{
    const char *val;
    if (compat_attr_get (h, "tbon.prefertcp", &val, NULL) < 0
        || val == NULL
        || streq (val, "0"))
        return false;
    return true;
}

/* Build a tcp:// URI with a wildcard port, taking into account the value of
 * tbon.interface-hint ("hostname", "default-route", or iface name/wildcard).
 */
static int format_tcp_uri (char *buf,
                           size_t size,
                           flux_t *h,
                           flux_error_t *error)
{
    const char *hint;
    int flags = 0;
    const char *interface = NULL;
    char ipaddr[_POSIX_HOST_NAME_MAX + 1];

    if (compat_attr_get (h, "tbon.interface-hint", &hint, NULL) < 0) {
        return errprintf (error,
                          "tbon.interface-hint attribute is not set: %s",
                          strerror (errno));
    }
    if (streq (hint, "hostname"))
        flags |= IPADDR_HOSTNAME;
    else if (streq (hint, "default-route"))
        ; // default behavior
    else
        interface = hint;
    if (getenv ("FLUX_IPADDR_V6"))
        flags |= IPADDR_V6;
    if (ipaddr_getprimary (ipaddr,
                           sizeof (ipaddr),
                           flags,
                           interface,
                           error) < 0) {
        return -1;
    }
    if (snprintf (buf, size, "tcp://%s:*", ipaddr) >= size) {
        errno = EOVERFLOW;
        errprintf (error, "tcp:// uri buffer overflow");
        return -1;
    }
    return 0;
}

/* Build an ipc:// uri consisting of rundir + "tbon-<rank>"
 */
static int format_ipc_uri (char *buf,
                           size_t size,
                           flux_t *h,
                           int rank,
                           flux_error_t *error)
{
    const char *rundir;

    if (compat_attr_get (h, "rundir", &rundir, NULL) < 0) {
        errprintf (error, "rundir attribute is not set");
        return -1;
    }
    if (snprintf (buf, size, "ipc://%s/tbon-%d", rundir, rank) >= size) {
        errno = EOVERFLOW;
        errprintf (error, "ipc:// uri buffer overflow");
        return -1;
    }
    return 0;
}

int boot_pmi (flux_t *h,
              uint32_t rank,
              uint32_t size,
              const char *hostname,
              struct overlay *overlay,
              flux_error_t *errp)
{
    const char *topo_uri;
    flux_error_t error;
    struct topology *topo = NULL;
    int child_count;
    int *child_ranks = NULL;
    struct taskmap *taskmap = NULL;
    const char *broker_mapping;

    // N.B. overlay_create() sets the tbon.topo attribute
    if (compat_attr_get (h, "tbon.topo", &topo_uri, NULL) < 0)
        return errprintf (errp, "error fetching tbon.topo attribute");
    if (!(topo = topology_create (topo_uri, size, NULL, &error))) {
        errprintf (errp,
                   "error creating '%s' topology: %s",
                   topo_uri,
                   error.text);
        goto error;
    }
    if (topology_set_rank (topo, rank) < 0
        || overlay_set_topology (overlay, topo) < 0) {
        errprintf (errp, "error setting rank/topology: %s", strerror (errno));
        goto error;
    }

    /* A size=1 instance has no peers, so skip the PMI exchange.
     */
    if (size == 1)
        goto done;

    /* Enable ipv6 for maximum flexibility in address selection.
     */
    overlay_set_ipv6 (overlay, 1);

    child_count = topology_get_child_ranks (topo, NULL, 0);
    if (child_count > 0) {
        if (!(child_ranks = calloc (child_count, sizeof (child_ranks[0])))
            || topology_get_child_ranks (topo, child_ranks, child_count) < 0) {
            errprintf (errp,
                       "error fetching child ranks from topology: %s",
                       strerror (errno));
            goto error;
        }
    }

    if (compat_attr_get (h, "broker.mapping", &broker_mapping, NULL) == 0) {
        if (broker_mapping
            && !(taskmap = taskmap_decode (broker_mapping, &error))) {
            errprintf (errp, "error decoding broker.mapping: %s", error.text);
            goto error;
        }
    }

    /* If there are to be downstream peers, then bind to a socket.
     * Depending on locality of children, use tcp://, ipc://, or both.
     */
    if (child_count > 0) {
        bool prefer_tcp = get_prefer_tcp (h);
        int nlocal;
        char tcp[1024];
        char ipc[1024];

        nlocal = clique_ranks (taskmap, rank, child_ranks, child_count);

        if (format_tcp_uri (tcp, sizeof (tcp), h, &error) < 0) {
            errprintf (errp, "%s", error.text);
            goto error;
        }
        if (format_ipc_uri (ipc, sizeof (ipc), h, rank, &error) < 0) {
            errprintf (errp, "%s", error.text);
            goto error;
        }
        if (prefer_tcp || nlocal == 0) {
            if (overlay_bind (overlay, tcp, NULL, errp) < 0)
                goto error;
        }
        else if (!prefer_tcp && nlocal == child_count) {
            if (overlay_bind (overlay, ipc, NULL, errp) < 0)
                goto error;
        }
        else {
            if (overlay_bind (overlay, tcp, ipc, errp) < 0)
                goto error;
        }
    }
    /* Each broker writes a business card consisting of hostname,
     * public key, and URIs (if any).
     */
    {
        const struct bizcard *bc;

        if (!(bc = overlay_get_bizcard (overlay))) {
            errprintf (errp, "get business card: %s", strerror (errno));
            goto error;
        }
        if (boot_util_iam (h, bc, errp) < 0)
            goto error;
        if (compat_attr_add (h,
                             "tbon.endpoint",
                             bizcard_uri_first (bc), // OK if NULL
                             ATTR_IMMUTABLE) < 0) {
            errprintf (errp, "setattr tbon.endpoint: %s", strerror (errno));
            goto error;
        }
    }

    /* BARRIER */
    if (boot_util_barrier (h, errp) < 0)
        goto error;

    /* Fetch the business card of parent and inform overlay of URI
     * and public key.
     */
    if (rank > 0) {
        int parent_rank = topology_get_parent (topo);
        const char *uri = NULL;
        struct bizcard *bc;

        if (!(bc = boot_util_whois_rank (h, parent_rank, errp)))
            goto error;
        if (!get_prefer_tcp (h)
            && clique_ranks (taskmap, rank, &parent_rank, 1) == 1)
            uri = bizcard_uri_find (bc, "ipc://");
        if (!uri)
            uri = bizcard_uri_find (bc, NULL);
        if (overlay_set_parent_uri (overlay, uri) < 0) {
            errprintf (errp, "overlay_set_parent_uri: %s", strerror (errno));
            bizcard_decref (bc);
            goto error;
        }
        if (overlay_set_parent_pubkey (overlay, bizcard_pubkey (bc)) < 0) {
            errprintf (errp, "overlay_set_parent_pubkey: %s", strerror (errno));
            bizcard_decref (bc);
            goto error;
        }
        bizcard_decref (bc);
    }

    /* Fetch the business card of children and inform overlay of public keys.
     */
    if (child_count > 0) {
        flux_future_t *f;
        int child_rank;
        struct bizcard *bc;
        char name[64];

        if (!(f = boot_util_whois (h, child_ranks, child_count, errp)))
            goto error;
        while ((child_rank = boot_util_whois_get_rank (f)) >= 0
            && (bc = boot_util_whois_get_bizcard (f)) != NULL) {
            (void)snprintf (name, sizeof (name), "%d", child_rank);
            if (overlay_authorize (overlay, name, bizcard_pubkey (bc)) < 0) {
                errprintf (errp,
                           "overlay_authorize %s=%s: %s",
                           name,
                           bizcard_pubkey (bc),
                           strerror (errno));
                bizcard_decref (bc);
                flux_future_destroy (f);
                goto error;
            }
            bizcard_decref (bc);
            flux_future_reset (f);
        }
        if (errno != ENODATA) {
            errprintf (errp, "bootstrap.whois: %s", future_strerror (f, errno));
            flux_future_destroy (f);
            goto error;
        }
        flux_future_destroy (f);
    }

    /* One more barrier before allowing connects to commence.
     * Need to ensure that all clients are "allowed".
     */
    if (boot_util_barrier (h, errp) < 0)
        goto error;
done:
    free (child_ranks);
    taskmap_destroy (taskmap);
    topology_decref (topo);
    return 0;
error:
    free (child_ranks);
    taskmap_destroy (taskmap);
    topology_decref (topo);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
