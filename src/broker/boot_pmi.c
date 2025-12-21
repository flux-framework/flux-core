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
#include <flux/hostlist.h>
#include <flux/taskmap.h>

#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/ipaddr.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libpmi/upmi.h"
#include "src/common/libpmi/bizcache.h"
#include "ccan/str/str.h"

#include "attr.h"
#include "overlay.h"
#include "topology.h"
#include "bootstrap.h"
#include "boot_pmi.h"


static int create_singleton_taskmap (struct taskmap **mp,
                                     flux_error_t *error)
{
    struct taskmap *map;
    flux_error_t e;

    if (!(map = taskmap_decode ("[[0,1,1,1]]", &e))) {
        errprintf (error, "error creating singleton taskmap: %s", e.text);
        return -1;
    }
    *mp = map;
    return 0;
}

/* Fetch key from the PMI server and decode it as a taskmap.
 * Return -1 with error filled if a parse error occurs.
 * Return 0 if map was properly parsed OR if it doesn't exist.
 */
static int fetch_taskmap_one (struct upmi *upmi,
                              const char *key,
                              struct taskmap **mp,
                              flux_error_t *error)
{
    struct taskmap *map;
    char *val;
    flux_error_t e;

    if (upmi_get (upmi, key, -1, &val, NULL) < 0) {
        *mp = NULL;
        return 0;
    }
    if (!(map = taskmap_decode (val, &e))) {
        errprintf (error, "%s: error decoding %s", key, e.text);
        free (val);
        return -1;
    }
    free (val);
    *mp = map;
    return 0;
}

static int fetch_taskmap (struct upmi *upmi,
                          struct taskmap **mp,
                          flux_error_t *error)
{
    struct taskmap *map;
    if (fetch_taskmap_one (upmi, "flux.taskmap", &map, error) < 0)
        return -1;
    if (map == NULL
        && fetch_taskmap_one (upmi, "PMI_process_mapping", &map, error) < 0)
        return -1;
    *mp = map; // might be NULL - that is OK
    return 0;
}

/* Set broker.mapping attribute.
 * It is not an error if the map is NULL.
 */
static int set_broker_mapping_attr (attr_t *attrs, struct taskmap *map)
{
    char *val = NULL;

    if (map && !(val = taskmap_encode (map, 0)))
        return -1;
    if (attr_add (attrs, "broker.mapping", val, ATTR_IMMUTABLE) < 0) {
        ERRNO_SAFE_WRAP (free, val);
        return -1;
    }
    free (val);
    return 0;
}

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
static bool get_prefer_tcp (attr_t *attrs)
{
    const char *val;
    if (attr_get (attrs, "tbon.prefertcp", &val, NULL) < 0
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
                           attr_t *attrs,
                           flux_error_t *error)
{
    const char *hint;
    int flags = 0;
    const char *interface = NULL;
    char ipaddr[_POSIX_HOST_NAME_MAX + 1];

    if (attr_get (attrs, "tbon.interface-hint", &hint, NULL) < 0) {
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
                           attr_t *attrs,
                           int rank,
                           flux_error_t *error)
{
    const char *rundir;

    if (attr_get (attrs, "rundir", &rundir, NULL) < 0) {
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

static int set_hostlist_attr (attr_t *attrs, struct hostlist *hl)
{
    const char *value;
    char *s;
    int rc = -1;

    /*  Allow hostlist attribute to be set on command line for testing.
     *  The value must be re-added if so, so that the IMMUTABLE flag can
     *   be set so that the attribute is properly cached.
     */
    if (attr_get (attrs, "hostlist", &value, NULL) == 0) {
        s = strdup (value);
        (void) attr_delete (attrs, "hostlist", true);
    }
    else
        s = hostlist_encode (hl);
    if (s && attr_add (attrs, "hostlist", s, ATTR_IMMUTABLE) == 0)
        rc = 0;
    ERRNO_SAFE_WRAP (free, s);
    return rc;
}

int boot_pmi (struct bootstrap *boot,
              struct upmi_info *info,
              const char *hostname,
              struct overlay *overlay,
              attr_t *attrs,
              flux_error_t *errp)
{
    const char *topo_uri;
    flux_error_t error;
    struct hostlist *hl = NULL;
    struct topology *topo = NULL;
    int child_count;
    int *child_ranks = NULL;
    int i;
    const struct bizcard *bc;
    struct taskmap *taskmap = NULL;

    // N.B. overlay_create() sets the tbon.topo attribute
    if (attr_get (attrs, "tbon.topo", &topo_uri, NULL) < 0)
        return errprintf (errp, "error fetching tbon.topo attribute");
    if (!(topo = topology_create (topo_uri, info->size, NULL, &error))) {
        errprintf (errp,
                   "error creating '%s' topology: %s",
                   topo_uri,
                   error.text);
        goto error;
    }
    if (topology_set_rank (topo, info->rank) < 0
        || overlay_set_topology (overlay, topo) < 0) {
        errprintf (errp, "error setting rank/topology: %s", strerror (errno));
        goto error;
    }
    if (!(hl = hostlist_create ())) {
        errprintf (errp, "error creating hostlist: %s", strerror (errno));
        goto error;
    }

    if (info->size == 1) {
        if (create_singleton_taskmap (&taskmap, &error) < 0) {
            errprintf (errp, "error creating taskmap: %s", error.text);
            goto error;
        }
    }
    else {
        if (fetch_taskmap (boot->upmi, &taskmap, &error) < 0) {
            errprintf (errp, "error fetching taskmap: %s", error.text);
            goto error;
        }
    }
    if (set_broker_mapping_attr (attrs, taskmap) < 0) {
        errprintf (errp,
                   "error setting broker.mapping attribute: %s",
                   strerror (errno));
        goto error;
    }

    /* A size=1 instance has no peers, so skip the PMI exchange.
     */
    if (info->size == 1) {
        if (hostlist_append (hl, hostname) < 0) {
            errprintf (errp, "hostlist_append: %s", strerror (errno));
            goto error;
        }
        goto done;
    }

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

    /* If there are to be downstream peers, then bind to a socket.
     * Depending on locality of children, use tcp://, ipc://, or both.
     */
    if (child_count > 0) {
        bool prefer_tcp = get_prefer_tcp (attrs);
        int nlocal;
        char tcp[1024];
        char ipc[1024];

        nlocal = clique_ranks (taskmap, info->rank, child_ranks, child_count);

        if (format_tcp_uri (tcp, sizeof (tcp), attrs, &error) < 0) {
            errprintf (errp, "%s", error.text);
            goto error;
        }
        if (format_ipc_uri (ipc, sizeof (ipc), attrs, info->rank, &error) < 0) {
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
    if (!(bc = overlay_get_bizcard (overlay))) {
        errprintf (errp, "get business card: %s", strerror (errno));
        goto error;
    }
    if (bizcache_put (boot->cache, info->rank, bc, &error) < 0) {
        errprintf (errp, "put business card: %s", error.text);
        goto error;
    }

    if (attr_add (attrs,
                  "tbon.endpoint",
                  bizcard_uri_first (bc), // OK if NULL
                  ATTR_IMMUTABLE) < 0) {
        errprintf (errp, "setattr tbon.endpoint: %s", strerror (errno));
        goto error;
    }

    /* BARRIER */
    if (upmi_barrier (boot->upmi, &error) < 0) {
        errprintf (errp,
                   "%s: barrier: %s",
                   upmi_describe (boot->upmi),
                   error.text);
        goto error;
    }

    /* Fetch the business card of parent and inform overlay of URI
     * and public key.
     */
    if (info->rank > 0) {
        int parent_rank = topology_get_parent (topo);
        const char *uri = NULL;

        if (bizcache_get (boot->cache, parent_rank, &bc, errp) < 0)
            goto error;
        if (!get_prefer_tcp (attrs)
            && clique_ranks (taskmap, info->rank, &parent_rank, 1) == 1)
            uri = bizcard_uri_find (bc, "ipc://");
        if (!uri)
            uri = bizcard_uri_find (bc, NULL);
        if (overlay_set_parent_uri (overlay, uri) < 0) {
            errprintf (errp, "overlay_set_parent_uri: %s", strerror (errno));
            goto error;
        }
        if (overlay_set_parent_pubkey (overlay, bizcard_pubkey (bc)) < 0) {
            errprintf (errp, "overlay_set_parent_pubkey: %s", strerror (errno));
            goto error;
        }
    }

    /* Fetch the business card of children and inform overlay of public keys.
     */
    for (i = 0; i < child_count; i++) {
        int child_rank = child_ranks[i];
        char name[64];

        if (bizcache_get (boot->cache, child_rank, &bc, errp) < 0)
            goto error;
        (void)snprintf (name, sizeof (name), "%d", i);
        if (overlay_authorize (overlay, name, bizcard_pubkey (bc)) < 0) {
            errprintf (errp,
                       "overlay_authorize %s=%s: %s",
                       name,
                       bizcard_pubkey (bc),
                       strerror (errno));
            goto error;
        }
    }

    /* Fetch the business card of all ranks and build hostlist.
     * The hostlist is built independently (and in parallel) on all ranks.
     */
    for (i = 0; i < info->size; i++) {
        if (bizcache_get (boot->cache, i, &bc, errp) < 0)
            goto error;
        if (hostlist_append (hl, bizcard_hostname (bc)) < 0) {
            errprintf (errp, "hostlist_append: %s", strerror (errno));
            goto error;
        }
    }

    /* One more barrier before allowing connects to commence.
     * Need to ensure that all clients are "allowed".
     */
    if (upmi_barrier (boot->upmi, &error) < 0) {
        errprintf (errp,
                   "%s: barrier: %s",
                   upmi_describe (boot->upmi),
                   error.text);
        goto error;
    }

done:
    if (set_hostlist_attr (attrs, hl) < 0) {
        errprintf (errp, "setattr hostlist: %s", strerror (errno));
        goto error;
    }
    if (upmi_finalize (boot->upmi, &error) < 0) {
        errprintf (errp,
                   "%s: finalize: %s",
                   upmi_describe (boot->upmi),
                   error.text);
        goto error;
    }
    hostlist_destroy (hl);
    free (child_ranks);
    taskmap_destroy (taskmap);
    topology_decref (topo);
    return 0;
error:
    /* N.B. Some implementations of abort may not return.
     */
    (void)upmi_abort (boot->upmi,
                      errp ? errp->text : "fatal bootstrap error",
                      NULL);
    hostlist_destroy (hl);
    free (child_ranks);
    taskmap_destroy (taskmap);
    topology_decref (topo);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
