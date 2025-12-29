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

#include "src/common/libfluxutil/conf_bootstrap.h"
#include "src/common/libyuarel/yuarel.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libpmi/bizcard.h"
#include "src/common/libpmi/bizcache.h"
#include "ccan/str/str.h"

#include "compat.h"
#include "overlay.h"
#include "topology.h"
#include "boot_util.h"
#include "boot_config.h"


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

int boot_config (flux_t *h,
                 uint32_t rank,
                 uint32_t size,
                 const char *hostname,
                 struct overlay *overlay,
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

    // N.B. overlay_create() sets the tbon.topo attribute
    if (compat_attr_get (h, "tbon.topo", &topo_uri, NULL) < 0) {
        errprintf (errp,
                   "error fetching tbon.topo attribute: %s",
                   strerror (errno));
        goto error;
    }
    if (!(topo_args = json_pack ("{s:O}", "hosts", hosts))
        || !(topo = topology_create (topo_uri, size, topo_args, &error))) {
        errprintf (errp,
                   "Error creating %s topology: %s",
                   topo_uri,
                   error.text);
        goto error;
    }
    if (topology_set_rank (topo, rank) < 0
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

    /* If broker has "downstream" peers, determine the URI to bind to
     * from the config and tell overlay.  Also, set the tbon.endpoint
     * attribute to the URI peers will connect to.  If broker has no
     * downstream peers, set tbon.endpoint to NULL.
     */
    if (topology_get_child_ranks (topo, NULL, 0) > 0
        && compat_attr_get (h, "broker.recovery-mode", NULL, NULL) < 0) {
        struct bizcard *bc;
        const char *my_uri;
        const char *bind_uri;

        if (!(bind_uri = getbindbyrank (hosts, rank, errp)))
            goto error;
        if (overlay_bind (overlay, bind_uri, NULL, errp) < 0)
            goto error;
        if (overlay_authorize (overlay,
                               overlay_cert_name (overlay),
                               overlay_cert_pubkey (overlay)) < 0) {
            errprintf (errp, "overlay_authorize: %s", strerror (errno));
            goto error;
        }

        if (!(bc = boot_util_whois_rank (h, rank, errp)))
            goto error;
        if (!(my_uri = bizcard_uri_first (bc))) {
            errprintf (errp,
                       "connect URI is undefined for rank %d", rank);
            bizcard_decref (bc);
            goto error;
        }
        if (compat_attr_add (h,
                             "tbon.endpoint",
                             my_uri,
                             ATTR_IMMUTABLE) < 0) {
            errprintf (errp,
                       "setattr tbon.endpoint %s: %s",
                       my_uri,
                       strerror (errno));
            bizcard_decref (bc);
            goto error;
        }
        bizcard_decref (bc);
    }
    else {
        if (compat_attr_add (h,
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
    if (rank > 0) {
        struct bizcard *bc;
        uint32_t parent_rank = topology_get_parent (topo);
        const char *parent_uri;

        if (!(bc = boot_util_whois_rank (h, parent_rank, errp)))
            goto error;
        if (!(parent_uri = bizcard_uri_first (bc))) {
            errprintf (errp,
                       "connect URI is undefined for rank %u", parent_rank);
            bizcard_decref (bc);
            goto error;
        }
        warn_of_invalid_host (h, parent_uri);
        if (overlay_set_parent_uri (overlay, parent_uri) < 0) {
            errprintf (errp,
                       "overlay_set_parent_uri %s: %s",
                       parent_uri,
                       strerror (errno));
            bizcard_decref (bc);
            goto error;
        }
        bizcard_decref (bc);
        if (overlay_set_parent_pubkey (overlay,
                                       overlay_cert_pubkey (overlay)) < 0) {
            errprintf (errp,
                       "overlay_set_parent_pubkey self: %s",
                       strerror (errno));
            goto error;
        }
    }

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
