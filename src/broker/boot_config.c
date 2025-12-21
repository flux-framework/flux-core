/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* boot_config.c - get broker wireup info from config file */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdbool.h>
#include <jansson.h>
#include <sys/param.h>
#include <flux/core.h>
#include <flux/hostlist.h>
#include <flux/taskmap.h>

#include "src/common/libfluxutil/conf_bootstrap.h"
#include "src/common/libyuarel/yuarel.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libpmi/bizcard.h"
#include "src/common/libpmi/bizcache.h"
#include "ccan/str/str.h"

#include "attr.h"
#include "overlay.h"
#include "topology.h"
#include "bootstrap.h"
#include "boot_config.h"


static int boot_config_attr (attr_t *attrs,
                             const char *hostname,
                             json_t *hosts,
                             flux_error_t *errp)
{
    struct hostlist *hl = NULL;
    struct taskmap *map = NULL;
    char *s = NULL;
    size_t index;
    json_t *value;
    char *val;
    int rv = -1;

    if (!(hl = hostlist_create ()))
        goto error;

    json_array_foreach (hosts, index, value) {
        const char *host;
        if (json_unpack (value, "{s:s}", "host", &host) < 0) {
            errprintf (errp, "Internal error [bootstrap]: missing host field");
            errno = EINVAL;
            goto error;
        }
        if (hostlist_append (hl, host) < 0) {
            errprintf (errp,
                       "Internal error [bootstrap]: hostlist_append: %s",
                       strerror (errno));
            goto error;
        }
    }

    if (!(s = hostlist_encode (hl)))
        goto error;

    if (attr_add (attrs,
                  "hostlist",
                  s,
                  ATTR_IMMUTABLE) < 0) {
        errprintf (errp,
                   "failed to set hostlist attribute to"
                   " config derived value: %s",
                   strerror (errno));
        goto error;
    }
    free (s);
    s = NULL;

    /* Generate broker.mapping.
     * For now, set it to NULL if there are multiple brokers per node.
     */
    hostlist_uniq (hl);
    if (hostlist_count (hl) < json_array_size (hosts))
        val = NULL;
    else {
        if (!(map = taskmap_create ())
            || taskmap_append (map, 0, json_array_size (hosts), 1) < 0) {
            errprintf (errp, "error creating taskmap: %s", strerror (errno));
            goto error;
        }
        if (!(s = taskmap_encode (map, 0))) {
            errprintf (errp, "error encoding broker.mapping");
            errno = EOVERFLOW;
            goto error;
        }
        val = s;
    }
    if (attr_add (attrs, "broker.mapping", val, ATTR_IMMUTABLE) < 0) {
        errprintf (errp, "setattr broker.mapping: %s", strerror (errno));
        goto error;
    }

    rv = 0;
error:
    taskmap_destroy (map);
    hostlist_destroy (hl);
    free (s);
    return rv;
}

/* Look up the host entry for 'rank' and return bind URI, if any.
 */
static const char *getbindbyrank (json_t *hosts,
                                  uint32_t rank,
                                  flux_error_t *errp)
{
    json_t *entry;
    const char *uri;

    if (!(entry = json_array_get (hosts, rank))
        || json_unpack (entry, "{s:s}", "bind", &uri) < 0) {
        errprintf (errp,
                   "bind URI is undefined for rank %u",
                   (unsigned int)rank);
        return NULL;
    }
    return uri;
}

static int set_broker_boot_method_attr (attr_t *attrs,
                                        const char *value,
                                        flux_error_t *errp)
{
    (void)attr_delete (attrs, "broker.boot-method", true);
    if (attr_add (attrs,
                  "broker.boot-method",
                  value,
                  ATTR_IMMUTABLE) < 0) {
        return errprintf (errp,
                          "setattr broker.boot-method: %s",
                          strerror (errno));
    }
    return 0;
}

/* Zeromq treats failed hostname resolution as a transient error, and silently
 * retries in the background, which can make config problems hard to diagnose.
 * Parse the URI in advance, and if the host portion is invalid, log it.
 * Ref: flux-framework/flux-core#5009
 */
static void warn_of_invalid_host (flux_t *h, const char *uri)
{
    char *cpy;
    struct yuarel u = { 0 };
    struct addrinfo *result;
    int e;

    if (!(cpy = strdup (uri))
        || yuarel_parse (&u, cpy) < 0
        || !u.scheme
        || !u.host
        || !streq (u.scheme, "tcp"))
        goto done;
    /* N.B. this URI will be used for zmq_connect(), therefore it must
     * be a valid peer address, not an interface name or wildcard.
     */
    if ((e = getaddrinfo (u.host, NULL, NULL, &result)) == 0) {
        freeaddrinfo (result);
        goto done;
    }
    flux_log (h,
              LOG_WARNING, "unable to resolve upstream peer %s: %s",
              u.host,
              gai_strerror (e));
done:
    free (cpy);
}

int boot_config (struct bootstrap *boot,
                 struct upmi_info *info,
                 flux_t *h,
                 const char *hostname,
                 struct overlay *overlay,
                 attr_t *attrs,
                 flux_error_t *errp)
{
    bool enable_ipv6 = false;
    const char *curve_cert = NULL;
    json_t *hosts = NULL;
    json_t *topo_args = NULL;
    struct topology *topo = NULL;
    flux_error_t error;
    const char *topo_uri;

    /* Ingest the [bootstrap] stanza.
     */
    if (conf_bootstrap_parse (flux_get_conf (h),
                              hostname,
                              &enable_ipv6,
                              &curve_cert,
                              &hosts,
                              errp) < 0)
        return -1;

    if (boot_config_attr (attrs, hostname, hosts, errp) < 0)
        goto error;

    // N.B. overlay_create() sets the tbon.topo attribute
    if (attr_get (attrs, "tbon.topo", &topo_uri, NULL) < 0) {
        errprintf (errp,
                   "error fetching tbon.topo attribute: %s",
                   strerror (errno));
        goto error;
    }
    if (!(topo_args = json_pack ("{s:O}", "hosts", hosts))
        || !(topo = topology_create (topo_uri,
                                     info->size,
                                     topo_args,
                                     &error))) {
        errprintf (errp,
                   "Error creating %s topology: %s",
                   topo_uri,
                   error.text);
        goto error;
    }
    if (topology_set_rank (topo, info->rank) < 0
        || overlay_set_topology (overlay, topo) < 0) {
        errprintf (errp,
                   "Error setting %s topology: %s",
                   topo_uri,
                   strerror (errno));
        goto error;
    }

    /* If a curve certificate was provided, load it.
     */
    if (curve_cert) {
        if (overlay_cert_load (overlay, curve_cert, &error) < 0) {
            errprintf (errp, "Error loading certificate: %s", error.text);
            goto error;
        }
    }

    /* If user requested ipv6, enable it here.
     * N.B. this prevents binding from interfaces that are IPv4 only (#3824).
     */
    overlay_set_ipv6 (overlay, enable_ipv6);

    /* Ensure that tbon.interface-hint is set.
     */
    if (overlay_set_tbon_interface_hint (overlay, NULL) < 0) {
        errprintf (errp,
                   "error setting tbon.interface-hint attribute: %s",
                   strerror (errno));
        goto error;
    }

    /* If broker has "downstream" peers, determine the URI to bind to
     * from the config and tell overlay.  Also, set the tbon.endpoint
     * attribute to the URI peers will connect to.  If broker has no
     * downstream peers, set tbon.endpoint to NULL.
     */
    if (topology_get_child_ranks (topo, NULL, 0) > 0
        && attr_get (attrs, "broker.recovery-mode", NULL, NULL) < 0) {
        const struct bizcard *bc;
        const char *bind_uri;
        const char *my_uri;

        if (!(bind_uri = getbindbyrank (hosts, info->rank, errp)))
            goto error;
        if (overlay_bind (overlay, bind_uri, NULL, errp) < 0)
            goto error;
        if (overlay_authorize (overlay,
                               overlay_cert_name (overlay),
                               overlay_cert_pubkey (overlay)) < 0) {
            errprintf (errp, "overlay_authorize: %s", strerror (errno));
            goto error;
        }
        if (bizcache_get (boot->cache, info->rank, &bc, errp) < 0
            || !(my_uri = bizcard_uri_first (bc))) {
            errprintf (errp,
                       "connect URI is undefined for rank %d", info->rank);
            goto error;
        }
        if (attr_add (attrs,
                      "tbon.endpoint",
                      my_uri,
                      ATTR_IMMUTABLE) < 0) {
            errprintf (errp,
                       "setattr tbon.endpoint %s: %s",
                       my_uri,
                       strerror (errno));
            goto error;
        }
    }
    else {
        if (attr_add (attrs,
                      "tbon.endpoint",
                      NULL,
                      ATTR_IMMUTABLE) < 0) {
            errprintf (errp,
                       "setattr tbon.endpoint NULL: %s",
                       strerror (errno));
            goto error;
        }
    }

    /* If broker has an "upstream" peer, determine its URI and tell overlay.
     */
    if (info->rank > 0) {
        const struct bizcard *bc;
        uint32_t parent_rank = topology_get_parent (topo);
        const char *parent_uri;

        if (bizcache_get (boot->cache, parent_rank, &bc, errp) < 0
            || !(parent_uri = bizcard_uri_first (bc))) {
            errprintf (errp,
                       "connect URI is undefined for rank %d", info->rank);
            goto error;
        }
        warn_of_invalid_host (h, parent_uri);
        if (overlay_set_parent_uri (overlay, parent_uri) < 0) {
            errprintf (errp,
                       "overlay_set_parent_uri %s: %s",
                       parent_uri,
                       strerror (errno));
            goto error;
        }
        if (overlay_set_parent_pubkey (overlay,
                                       overlay_cert_pubkey (overlay)) < 0) {
            errprintf (errp,
                       "overlay_set_parent_pubkey self: %s",
                       strerror (errno));
            goto error;
        }
    }

    /* instance-level (position in instance hierarchy) is always zero here.
     */
    if (attr_add (attrs,
                  "instance-level",
                  "0",
                  ATTR_IMMUTABLE) < 0) {
        errprintf (errp, "setattr instance-level 0: %s", strerror (errno));
        goto error;
    }
    if (set_broker_boot_method_attr (attrs, "config", errp) < 0)
        goto error;
    json_decref (hosts);
    json_decref (topo_args);
    topology_decref (topo);
    return 0;
error:
    ERRNO_SAFE_WRAP (json_decref, hosts);
    ERRNO_SAFE_WRAP (json_decref, topo_args);
    ERRNO_SAFE_WRAP (topology_decref, topo);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
