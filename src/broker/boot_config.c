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
#include <unistd.h>
#include <limits.h>
#include <stdbool.h>
#include <argz.h>
#include <fnmatch.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/ipaddr.h"
#include "src/common/libutil/kary.h"

#include "attr.h"
#include "overlay.h"
#include "boot_config.h"

/* Attributes that may not be set for this boot method.
 */
static const char *broker_attr_blacklist[] = {
    "tbon.endpoint",
    NULL,
};

static int parse_endpoint_by_rank (const json_t *endpoints,
                                   int rank,
                                   const char **entry)
{
    const json_t *value;
    const char *s;

    if (!(value = json_array_get (endpoints, rank))) {
        log_msg ("Config file error [bootstrap]: rank out of range");
        return -1;
    }
    if (!(s = json_string_value (value))) {
        log_msg ("Config file error [bootstrap]: malformed tbon-endpoints");
        log_msg ("Hint: all array entries must be strings");
        return -1;
    }
    *entry = s;
    return 0;
}

/* Search array of configured endpoints, ordered by rank, for one that
 * matches a local address.  For testing support, greedily match any ipc://
 * or tcp://127.0.0.0 address.  On success, array index (rank) is returned.
 * On failure, -1 is returned with diagnostics on stderr.
 */
static int find_local_endpoint (const json_t *endpoints)
{
    char *addrs = NULL;
    size_t addrs_len = 0;
    char error[200];
    int rank;
    const json_t *value;

    if (ipaddr_getall (&addrs, &addrs_len, error, sizeof (error)) < 0) {
        log_msg ("%s", error);
        return -1;
    }
    json_array_foreach (endpoints, rank, value) {
        const char *s = json_string_value (value);
        const char *entry = NULL;

        if (!s) // array element is not a string
            break;
        if (fnmatch ("tcp://127.0.0.*:*", s, 0) == 0)
            break;
        if (fnmatch("ipc://*", s, 0) == 0)
            break;
        while ((entry = argz_next (addrs, addrs_len, entry))) {
            char pat[128];
            snprintf (pat, sizeof (pat), "tcp://%s:*", entry);
            if (fnmatch (pat, s, 0) == 0)
                break;
        }
        if (entry != NULL) // found a match in 'addrs'
            break;
    }
    free (addrs);
    if (rank == json_array_size (endpoints))
        return -1;

    return rank;
}

static int parse_config_file (flux_t *h,
                              int *rankp,
                              int *sizep,
                              const json_t **endpointsp)
{
    const flux_conf_t *conf;
    int size = INT_MAX; // optional in config file
    int rank = INT_MAX; // optional in config file
    const json_t *endpoints;
    flux_conf_error_t error;

    /* N.B. the broker parses config early, and treats missing/malformed
     * TOML as a fatal error.  The checks that we make here are specific
     * to the [bootstrap] section.
     */
    conf = flux_get_conf (h, NULL);

    if (flux_conf_unpack (conf,
                          &error,
                          "{s:{s?:i s?:i s:o}}",
                          "bootstrap",
                            "size", &size,
                            "rank", &rank,
                            "tbon-endpoints", &endpoints) < 0) {
        log_msg ("Config file error [bootstrap]: %s", error.errbuf);
        return -1;
    }
    if (json_array_size (endpoints) == 0) { // returns 0 if non-array
        log_msg ("Config file error [bootstrap]: malformed tbon-endpoints");
        log_msg ("Hint: must be an array with at least one element.");
        return -1;
    }
    /* If size and/or rank were unspecified, infer them by looking at the
     * size of the tbon-endpoints array, and/or by scanning the array for
     * an address that matches a local interface.
     */
    if (size == INT_MAX)
        size = json_array_size (endpoints);
    if (rank == INT_MAX) {
        if ((rank = find_local_endpoint (endpoints)) < 0) {
            log_msg ("Config file error [bootstrap]: could not determine rank");
            log_msg ("Hint: set rank in config file or ensure tbon-endpoints");
            log_msg ("  contains a local address");
            return -1;
        }
    }
    if (rank < 0 || rank > size - 1) {
        log_msg ("Config file error [bootstrap]: invalid rank %d for size %d",
                 rank,
                 size);
        return -1;
    }
    *rankp = rank;
    *sizep = size;
    *endpointsp = endpoints;
    return 0;
}

/* Find attributes that, if set, should cause a fatal error.  Unlike PMI boot,
 * this method has no mechanism to share info with other ranks before
 * bootstrap/wireup completes.  Therefore, all bootstrap info has to come
 * from the shared config file.  Return 0 on success (no bad attrs),
 * or -1 on failure (one or more), with diagnostics on stderr.
 */
static int find_blacklist_attrs (attr_t *attrs)
{
    int errors = 0;
    const char **cp;

    for (cp = &broker_attr_blacklist[0]; *cp != NULL; cp++) {
        if (attr_get (attrs, *cp, NULL, NULL) == 0) {
            log_msg ("attribute %s may not be set with boot_method=config",
                     *cp);
            errors++;
        }
    }
    if (errors > 0)
        return -1;
    return 0;
}

int boot_config (flux_t *h, overlay_t *overlay, attr_t *attrs, int tbon_k)
{
    int size;
    int rank;
    const json_t *endpoints;
    const char *this_endpoint;

    if (find_blacklist_attrs (attrs) < 0)
        return -1;
    if (parse_config_file (h, &rank, &size, &endpoints) < 0)
        return -1;

    /* Initialize overlay network parameters.
     */
    if (overlay_init (overlay, size, rank, tbon_k) < 0)
        return -1;
    if (parse_endpoint_by_rank (endpoints, rank, &this_endpoint) < 0)
        return -1;
    if (overlay_set_child (overlay, this_endpoint) < 0) {
        log_err ("overlay_set_child %s", this_endpoint);
        return -1;
    }
    if (rank > 0) {
        int parent_rank = kary_parentof (tbon_k, rank);
        const char *parent_endpoint;

        if (parse_endpoint_by_rank (endpoints,
                                    parent_rank,
                                    &parent_endpoint) < 0)
            return -1;
        if (overlay_set_parent (overlay, parent_endpoint) < 0) {
            log_err ("overlay_set_parent %s", parent_endpoint);
            return -1;
        }
    }

    /* Update attributes.
     */
    if (attr_add (attrs,
                  "tbon.endpoint",
                  this_endpoint,
                  FLUX_ATTRFLAG_IMMUTABLE) < 0) {
        log_err ("setattr tbon.endpoint %s", this_endpoint);
        return -1;
    }
    if (attr_add (attrs,
                  "instance-level",
                  "0",
                  FLUX_ATTRFLAG_IMMUTABLE) < 0) {
        log_err ("setattr instance-level 0");
        return -1;
    }

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
