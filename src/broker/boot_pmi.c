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

#include "src/common/libutil/log.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/ipaddr.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libpmi/upmi.h"
#include "ccan/str/str.h"

#include "attr.h"
#include "overlay.h"
#include "topology.h"
#include "boot_pmi.h"


/*  If the broker is launched via flux-shell, then the shell may opt
 *  to set a "flux.instance-level" parameter in the PMI kvs to tell
 *  the booting instance at what "level" it will be running, i.e. the
 *  number of parents. If the PMI key is missing, this is not an error,
 *  instead the level of this instance is considered to be zero.
 */
static int set_instance_level_attr (struct upmi *upmi,
                                    attr_t *attrs,
                                    bool *under_flux)
{
    char *val = NULL;
    int rc = -1;

    (void)upmi_get (upmi, "flux.instance-level", -1, &val, NULL);
    if (attr_add (attrs, "instance-level", val ? val : "0", ATTR_IMMUTABLE) < 0)
        goto error;
    *under_flux = val ? true : false;
    rc = 0;
error:
    ERRNO_SAFE_WRAP (free, val);
    return rc;
}

/* If the tbon.interface-hint broker attr is not already set, set it.
 * If running under Flux, use the value, if any, placed in PMI KVS by
 * the enclsoing instance.  Otherwise, set a default value.
 */
static int set_tbon_interface_hint_attr (struct upmi *upmi,
                                         attr_t *attrs,
                                         struct overlay *ov,
                                         bool under_flux)
{
    char *val = NULL;
    int rc = -1;

    if (attr_get (attrs, "tbon.interface-hint", NULL, NULL) == 0)
        return 0;
    if (under_flux)
        (void)upmi_get (upmi, "flux.tbon-interface-hint", -1, &val, NULL);
    if (overlay_set_tbon_interface_hint (ov, val) < 0)
        goto error;
    rc = 0;
error:
    ERRNO_SAFE_WRAP (free, val);
    return rc;
}

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
        log_err ("tbon.interface-hint attribute is not set");
        return -1;
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

static int set_broker_boot_method_attr (attr_t *attrs, const char *value)
{
    (void)attr_delete (attrs, "broker.boot-method", true);
    if (attr_add (attrs,
                  "broker.boot-method",
                  value,
                  ATTR_IMMUTABLE) < 0) {
        log_err ("setattr broker.boot-method");
        return -1;
    }
    return 0;
}

/* Small business card cache, by rank.
 * Business cards are fetched one by one from the PMI server.  To avoid
 * fetching the same ones more than once in different parts of the code,
 * implement a simple cache.
 */
struct bizcache {
    size_t size;
    struct bizcard **cards; // indexed by rank, array storage follows struct
};

static struct bizcache *bizcache_create (size_t size)
{
    struct bizcache *cache;

    cache = calloc (1, sizeof (*cache) + size * sizeof (struct bizcard *));
    if (!cache)
        return NULL;
    cache->size = size;
    cache->cards = (struct bizcard **)(cache + 1);
    return cache;
}

static void bizcache_destroy (struct bizcache *cache)
{
    if (cache) {
        int saved_errno = errno;
        for (int i = 0; i < cache->size; i++)
            bizcard_decref (cache->cards[i]);
        free (cache);
        errno = saved_errno;
    }
}

static struct bizcard *bizcache_lookup (struct bizcache *cache, int rank)
{
    if (rank < 0 || rank >= cache->size)
        return NULL;
    return cache->cards[rank];
}

static int bizcache_insert (struct bizcache *cache,
                            int rank,
                            struct bizcard *bc)
{
    if (rank < 0 || rank >= cache->size) {
        errno = EINVAL;
        return -1;
    }
    cache->cards[rank] = bc;
    return 0;
}

/* Put business card directly to PMI using rank as key.
 */
static int put_bizcard (struct upmi *upmi,
                        int rank,
                        const struct bizcard *bc,
                        flux_error_t *error)
{
    char key[64];
    const char *s;
    flux_error_t e;

    (void)snprintf (key, sizeof (key), "%d", rank);
    if (!(s = bizcard_encode (bc))) {
        errprintf (error, "error encoding business card: %s", strerror (errno));
        return -1;
    }
    if (upmi_put (upmi, key, s, &e) < 0) {
        errprintf (error,
                   "%s: put %s: %s",
                   upmi_describe (upmi),
                   key,
                   e.text);
        return -1;
    }
    return 0;
}

/* Return business card from cache, filling the cache entry by fetching
 * it from PMI if missing.  The caller must not free the business card.
 */
static int get_bizcard (struct upmi *upmi,
                        struct bizcache *cache,
                        int rank,
                        const struct bizcard **bcp,
                        flux_error_t *error)
{
    char key[64];
    char *val;
    flux_error_t e;
    struct bizcard *bc;

    if ((bc = bizcache_lookup (cache, rank))) {
        *bcp = bc;
        return 0;
    }

    (void)snprintf (key, sizeof (key), "%d", rank);
    if (upmi_get (upmi, key, rank, &val, &e) < 0) {
        errprintf (error,
                   "%s: get %s: %s",
                   upmi_describe (upmi),
                   key,
                   e.text);
        return -1;
    }
    if (!(bc = bizcard_decode (val, &e))) {
        errprintf (error,
                   "error decoding rank %d business card: %s",
                   rank,
                   e.text);
        goto error;
    }
    if (bizcache_insert (cache, rank, bc) < 0) {
        errprintf (error, "error caching rank %d business card", rank);
        bizcard_decref (bc);
        goto error;
    }
    free (val);
    *bcp = bc;
    return 0;
error:
    ERRNO_SAFE_WRAP (free, val);
    return -1;
}

static void trace_upmi (void *arg, const char *text)
{
    fprintf (stderr, "boot_pmi: %s\n", text);
}

int boot_pmi (const char *hostname, struct overlay *overlay, attr_t *attrs)
{
    const char *topo_uri;
    flux_error_t error;
    struct hostlist *hl = NULL;
    struct upmi *upmi;
    struct upmi_info info;
    struct topology *topo = NULL;
    int child_count;
    int *child_ranks = NULL;
    int i;
    int upmi_flags = UPMI_LIBPMI_NOFLUX;
    const char *upmi_method;
    bool under_flux;
    const struct bizcard *bc;
    struct bizcache *cache = NULL;
    struct taskmap *taskmap = NULL;
    const char *dkey;
    json_t *value;

    // N.B. overlay_create() sets the tbon.topo attribute
    if (attr_get (attrs, "tbon.topo", &topo_uri, NULL) < 0) {
        log_msg ("error fetching tbon.topo attribute");
        return -1;
    }
    if (attr_get (attrs, "broker.boot-method", &upmi_method, NULL) < 0)
        upmi_method = NULL;
    if (getenv ("FLUX_PMI_DEBUG"))
        upmi_flags |= UPMI_TRACE;
    if (!(upmi = upmi_create (upmi_method,
                              upmi_flags,
                              trace_upmi,
                              NULL,
                              &error))) {
        log_msg ("boot_pmi: %s", error.text);
        return -1;
    }
    if (upmi_initialize (upmi, &info, &error) < 0) {
        log_msg ("%s: initialize: %s", upmi_describe (upmi), error.text);
        upmi_destroy (upmi);
        return -1;
    }
    if (info.dict != NULL) {
        json_object_foreach(info.dict, dkey, value) {
            if (!json_is_string(value)) {
                log_err ("%s: initialize: value associated to key %s is not a string", upmi_describe (upmi), dkey);
                goto error;
            }
            if (attr_add (attrs, dkey, json_string_value(value), ATTR_IMMUTABLE) < 0) {
                log_err("%s: initialize: could not put attribute for key %s", upmi_describe (upmi), dkey);
                goto error;
            }
        }
    }
    if (set_instance_level_attr (upmi, attrs, &under_flux) < 0) {
        log_err ("set_instance_level_attr");
        goto error;
    }
    if (under_flux) {
        if (attr_add (attrs, "jobid", info.name, ATTR_IMMUTABLE) < 0) {
            log_err ("error setting jobid attribute");
            goto error;
        }
    }
    if (set_tbon_interface_hint_attr (upmi, attrs, overlay, under_flux) < 0) {
        log_err ("error setting tbon.interface-hint attribute");
        goto error;
    }
    if (!(topo = topology_create (topo_uri, info.size, &error))) {
        log_msg ("error creating '%s' topology: %s", topo_uri, error.text);
        goto error;
    }
    if (topology_set_rank (topo, info.rank) < 0
        || overlay_set_topology (overlay, topo) < 0)
        goto error;
    if (!(hl = hostlist_create ())) {
        log_err ("hostlist_create");
        goto error;
    }

    if (info.size == 1) {
        if (create_singleton_taskmap (&taskmap, &error) < 0)
            goto error;
    }
    else {
        if (fetch_taskmap (upmi, &taskmap, &error) < 0)
            goto error;
    }
    if (set_broker_mapping_attr (attrs, taskmap) < 0) {
        log_err ("error setting broker.mapping attribute");
        goto error;
    }

    /* A size=1 instance has no peers, so skip the PMI exchange.
     */
    if (info.size == 1) {
        if (hostlist_append (hl, hostname) < 0) {
            log_err ("hostlist_append");
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
            || topology_get_child_ranks (topo, child_ranks, child_count) < 0)
            goto error;
    }

    /* If there are to be downstream peers, then bind to a socket.
     * Depending on locality of children, use tcp://, ipc://, or both.
     */
    if (child_count > 0) {
        bool prefer_tcp = get_prefer_tcp (attrs);
        int nlocal;
        char tcp[1024];
        char ipc[1024];

        nlocal = clique_ranks (taskmap, info.rank, child_ranks, child_count);

        if (format_tcp_uri (tcp, sizeof (tcp), attrs, &error) < 0) {
            log_err ("%s", error.text);
            goto error;
        }
        if (format_ipc_uri (ipc, sizeof (ipc), attrs, info.rank, &error) < 0) {
            log_err ("%s", error.text);
            goto error;
        }
        if (prefer_tcp || nlocal == 0) {
            if (overlay_bind (overlay, tcp, NULL) < 0)
                goto error;
        }
        else if (!prefer_tcp && nlocal == child_count) {
            if (overlay_bind (overlay, ipc, NULL) < 0)
                goto error;
        }
        else {
            if (overlay_bind (overlay, tcp, ipc) < 0)
                goto error;
        }
    }

    /* Each broker writes a business card consisting of hostname,
     * public key, and URIs (if any).
     */
    if (!(bc = overlay_get_bizcard (overlay)))
        goto error;
    if (put_bizcard (upmi, info.rank, bc, &error) < 0) {
        log_msg ("%s", error.text);
        goto error;
    }

    if (attr_add (attrs,
                  "tbon.endpoint",
                  bizcard_uri_first (bc), // OK if NULL
                  ATTR_IMMUTABLE) < 0) {
        log_err ("setattr tbon.endpoint");
        goto error;
    }

    /* BARRIER */
    if (upmi_barrier (upmi, &error) < 0) {
        log_msg ("%s: barrier: %s", upmi_describe (upmi), error.text);
        goto error;
    }

    /* Cache bizcard results by rank to avoid repeated PMI lookups.
     */
    if (!(cache = bizcache_create (info.size)))
        goto error;

    /* Fetch the business card of parent and inform overlay of URI
     * and public key.
     */
    if (info.rank > 0) {
        int parent_rank = topology_get_parent (topo);
        const char *uri = NULL;

        if (get_bizcard (upmi, cache, parent_rank, &bc, &error) < 0) {
            log_msg ("%s", error.text);
            goto error;
        }
        if (!get_prefer_tcp (attrs)
            && clique_ranks (taskmap, info.rank, &parent_rank, 1) == 1)
            uri = bizcard_uri_find (bc, "ipc://");
        if (!uri)
            uri = bizcard_uri_find (bc, NULL);
        if (overlay_set_parent_uri (overlay, uri) < 0) {
            log_err ("overlay_set_parent_uri");
            goto error;
        }
        if (overlay_set_parent_pubkey (overlay, bizcard_pubkey (bc)) < 0) {
            log_err ("overlay_set_parent_pubkey");
            goto error;
        }
    }

    /* Fetch the business card of children and inform overlay of public keys.
     */
    for (i = 0; i < child_count; i++) {
        int child_rank = child_ranks[i];
        char name[64];

        if (get_bizcard (upmi, cache, child_rank, &bc, &error) < 0) {
            log_msg ("%s", error.text);
            goto error;
        }
        (void)snprintf (name, sizeof (name), "%d", i);
        if (overlay_authorize (overlay, name, bizcard_pubkey (bc)) < 0) {
            log_err ("overlay_authorize %s=%s", name, bizcard_pubkey (bc));
            goto error;
        }
    }

    /* Fetch the business card of all ranks and build hostlist.
     * The hostlist is built independently (and in parallel) on all ranks.
     */
    for (i = 0; i < info.size; i++) {
        if (get_bizcard (upmi, cache, i, &bc, &error) < 0) {
            log_msg ("%s", error.text);
            goto error;
        }
        if (hostlist_append (hl, bizcard_hostname (bc)) < 0) {
            log_err ("hostlist_append");
            goto error;
        }
    }

    /* One more barrier before allowing connects to commence.
     * Need to ensure that all clients are "allowed".
     */
    if (upmi_barrier (upmi, &error) < 0) {
        log_msg ("%s: barrier: %s", upmi_describe (upmi), error.text);
        goto error;
    }

done:
    if (set_hostlist_attr (attrs, hl) < 0) {
        log_err ("setattr hostlist");
        goto error;
    }
    if (set_broker_boot_method_attr (attrs, upmi_describe (upmi)) < 0)
        goto error;
    if (upmi_finalize (upmi, &error) < 0) {
        log_msg ("%s: finalize: %s", upmi_describe (upmi), error.text);
        goto error;
    }
    upmi_destroy (upmi);
    hostlist_destroy (hl);
    free (child_ranks);
    taskmap_destroy (taskmap);
    topology_decref (topo);
    bizcache_destroy (cache);
    return 0;
error:
    /* We've logged error to stderr before getting here so the fatal
     * error message passed to the PMI server does not necessarily need
     * to be highly detailed.  Some implementations of abort may not
     * return.
     */
    if (upmi_abort (upmi, "fatal bootstrap error", &error) < 0)
        log_msg ("upmi_abort: %s", error.text);
    upmi_destroy (upmi);
    hostlist_destroy (hl);
    free (child_ranks);
    taskmap_destroy (taskmap);
    topology_decref (topo);
    bizcache_destroy (cache);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
