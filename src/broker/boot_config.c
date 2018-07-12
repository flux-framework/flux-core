/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <stdbool.h>
#include <argz.h>
#include <fnmatch.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/cf.h"
#include "src/common/libutil/ipaddr.h"
#include "src/common/libutil/kary.h"

#include "attr.h"
#include "overlay.h"
#include "boot_config.h"

/* Attributes that may not be set for this boot method.
 */
static const char *badat[] = {
    "tbon.endpoint",
    "mcast.endpoint",
    "session-id",
    NULL,
};

/* Config file table expectations.
 */
static const struct cf_option opts[] = {
    { "tbon-endpoints", CF_ARRAY, true },
    { "mcast-endpoint", CF_STRING, false },
    { "session-id", CF_STRING, true },
    { "rank", CF_INT64, false },
    { "size", CF_INT64, false },
    CF_OPTIONS_TABLE_END,
};

static int get_cf_endpoints_size (const cf_t *cf)
{
    const cf_t *endpoints = cf_get_in (cf, "tbon-endpoints");
    return cf_array_size (endpoints);
}

static const char *get_cf_endpoint (const cf_t *cf, int rank)
{
    const cf_t *endpoints = cf_get_in (cf, "tbon-endpoints");
    return cf_string (cf_get_at (endpoints, rank));
}

/* Search array of configured endpoints, ordered by rank, for one that
 * matches a local address.  For testing support, greedily match any ipc://
 * or tcp://127.0.0.0 address.  On success, array index (rank) is returned.
 * On failure, -1 is returned with diagnostics on stderr.
 */
static int find_local_cf_endpoint (const cf_t *cf, int size)
{
    char *addrs = NULL;
    size_t addrs_len = 0;
    char error[200];
    int i;

    if (ipaddr_getall (&addrs, &addrs_len, error, sizeof (error)) < 0) {
        log_msg ("%s", error);
        return -1;
    }
    for (i = 0; i < size; i++) {
        const char *s = get_cf_endpoint (cf, i);
        const char *entry = NULL;

        if (!s) // short array
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
    if (i == size) {
        log_msg ("local address not found in tbon-endpoints array");
        return -1;
    }
    return i;
}

/* Parse the config file, returning a table, pre-checked for
 * required keys and types.  Caller must free with cf_destroy().
 */
static cf_t *parse_config_file (attr_t *attrs)
{
    cf_t *cf = NULL;
    const char *path;
    struct cf_error error;

    if (attr_get (attrs, "boot.config_file", &path, NULL) < 0) {
        log_err ("getattr boot.config_file");
        goto error;
    }
    if (!(cf = cf_create ())) {
        log_err ("cf_create");
        goto error;
    }
    if (cf_update_file (cf, path, &error) < 0)
        goto error_cfmsg;
    if (cf_check (cf, opts, CF_STRICT, &error) < 0)
        goto error_cfmsg;
    return cf;
error_cfmsg:
    if (errno == EINVAL) {
        if (strlen (error.filename) == 0)
            log_msg ("%s", error.errbuf);
        else
            log_msg ("%s::%d: %s", error.filename, error.lineno, error.errbuf);
    } else
        log_err ("%s", path);
error:
    cf_destroy (cf);
    return NULL;
}

/* Find attributes that, if set, should cause a fatal error.  Unlike PMI boot,
 * this method has no mechanism to share info with other ranks before
 * bootstrap/wireup completes.  Therefore, all bootstrap info has to come
 * from the shared config file.  Return 0 on success (no bad attrs),
 * or -1 on failure (one or more), with diagnostics on stderr.
 */
static int find_incompat_attrs (attr_t *attrs)
{
    int i;
    int errors = 0;

    for (i = 0; badat[i] != NULL; i++) {
        if (attr_get (attrs, badat[i], NULL, NULL) == 0) {
            log_msg ("%s may not be set with boot_method=config", badat[i]);
            errors++;
        }
    }
    return errors > 0 ? -1 : 0;
}

int boot_config (overlay_t *overlay, attr_t *attrs, int tbon_k)
{
    int rc = -1;
    cf_t *cf = NULL;
    const cf_t *tmp;
    int64_t size;
    int64_t rank;

    if (find_incompat_attrs (attrs) < 0)
        return -1;
    if (!(cf = parse_config_file (attrs)))
        return -1;

    /* rank and size are optional in the config file.
     * If unspecified, infer from tbon-endpoint array.
     */
    if ((tmp = cf_get_in (cf, "size")))
        size = cf_int64 (tmp);
    else
        size = get_cf_endpoints_size (cf);
    if ((tmp = cf_get_in (cf, "rank")))
        rank = cf_int64 (tmp);
    else
        rank = find_local_cf_endpoint (cf, size);
    if (rank < 0 || rank > size - 1) {
        log_err ("invalid rank %d size %d", (int)rank, (int)size);
        goto done;
    }

    /* Initialize overlay network parameters.
     * N.B. mcast relay for cliques is not supported by this boot method.
     */
    overlay_init (overlay, size, rank, tbon_k);
    overlay_set_child (overlay, get_cf_endpoint (cf, rank));
    if (rank > 0) {
        int prank = kary_parentof (tbon_k, rank);
        overlay_set_parent (overlay, get_cf_endpoint (cf, prank));
    }
    if ((tmp = cf_get_in (cf, "mcast-endpoint"))
                        && strcmp (cf_string (tmp), "tbon") != 0)
        overlay_set_event (overlay, cf_string (tmp));

    /* Update attributes.
     */
    if (attr_add (attrs, "session-id", cf_string (cf_get_in (cf, "session-id")),
                  FLUX_ATTRFLAG_IMMUTABLE) < 0) {
        log_err ("setattr session-id");
        goto done;
    }
    if (attr_add (attrs, "tbon.endpoint", get_cf_endpoint (cf, rank),
                  FLUX_ATTRFLAG_IMMUTABLE) < 0) {
        log_err ("setattr tbon.endpoint");
        goto done;
    }
    tmp = cf_get_in (cf, "mcast-endpoint");
    if (attr_add (attrs, "mcast.endpoint", tmp ? cf_string (tmp) : "tbon",
                  FLUX_ATTRFLAG_IMMUTABLE) < 0) {
        log_err ("setattr mcast.endpoint");
        goto done;
    }

    rc = 0;
done:
    cf_destroy (cf);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
