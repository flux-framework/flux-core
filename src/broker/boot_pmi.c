/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
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
#include <sys/param.h>
#include <unistd.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/ipaddr.h"
#include "src/common/libutil/kary.h"
#include "src/common/libpmi/pmi.h"
#include "src/common/libpmi/pmi_strerror.h"

#include "attr.h"
#include "overlay.h"
#include "boot_pmi.h"

/* Generally accepted max, although some go higher (IE is 2083) */
#define ENDPOINT_MAX 2048

/* Given a string with possible format specifiers, return string that is
 * fully expanded.
 *
 * Possible format specifiers:
 * - %h - IP address of current hostname
 * - %B - value of attribute broker.rundir
 *
 * Caller is responsible for freeing memory of returned value.
 */
static char * format_endpoint (attr_t *attrs, const char *endpoint)
{
    char ipaddr[HOST_NAME_MAX + 1];
    char *ptr, *buf, *rv = NULL;
    bool percent_flag = false;
    unsigned int len = 0;
    const char *rundir;
    char error[200];

    buf = xzmalloc (ENDPOINT_MAX + 1);

    ptr = (char *)endpoint;
    while (*ptr) {
        if (percent_flag) {
            if (*ptr == 'h') {
                if (ipaddr_getprimary (ipaddr, sizeof (ipaddr),
                                       error, sizeof (error)) < 0) {
                    log_msg ("%s", error);
                    goto done;
                }
                if ((len + strlen (ipaddr)) > ENDPOINT_MAX) {
                    log_msg ("ipaddr overflow max endpoint length");
                    goto done;
                }
                strcat (buf, ipaddr);
                len += strlen (ipaddr);
            }
            else if (*ptr == 'B') {
                if (attr_get (attrs, "broker.rundir", &rundir, NULL) < 0) {
                    log_msg ("broker.rundir attribute is not set");
                    goto done;
                }
                if ((len + strlen (rundir)) > ENDPOINT_MAX) {
                    log_msg ("broker.rundir overflow max endpoint length");
                    goto done;
                }
                strcat (buf, rundir);
                len += strlen (rundir);
            }
            else if (*ptr == '%')
                buf[len++] = '%';
            else {
                buf[len++] = '%';
                buf[len++] = *ptr;
            }
            percent_flag = false;
        }
        else {
            if (*ptr == '%')
                percent_flag = true;
            else
                buf[len++] = *ptr;
        }

        if (len >= ENDPOINT_MAX) {
            log_msg ("overflow max endpoint length");
            goto done;
        }

        ptr++;
    }

    rv = buf;
done:
    if (!rv)
        free (buf);
    return (rv);
}

/* Process attribute with format_endpoint(), writing it back to the
 * attribute cache, then returning it in 'value'.
 * If attribute was not initially set, start with 'default_value'.
 * Return 0 on success, -1 on failure with diagnostics to stderr.
 */
static int update_endpoint_attr (attr_t *attrs, const char *name,
                                 const char **value, const char *default_value)
{
    const char *val;
    char *fmt_val = NULL;
    int rc = -1;

    if (attr_get (attrs, name, &val, NULL) < 0)
        val = default_value;
    if (!(fmt_val = format_endpoint (attrs, val))) {
        log_msg ("malformed %s: %s", name, val);
        return -1;
    }
    (void)attr_delete (attrs, name, true);
    if (attr_add (attrs, name, fmt_val, FLUX_ATTRFLAG_IMMUTABLE) < 0) {
        log_err ("setattr %s", name);
        goto done;
    }
    if (attr_get (attrs, name, &val, NULL) < 0)
        goto done;
    *value = val;
    rc = 0;
done:
    free (fmt_val);
    return rc;
}

int boot_pmi (overlay_t *overlay, attr_t *attrs, int tbon_k)
{
    int spawned;
    int size;
    int rank;
    int appnum;
    int relay_rank = -1;
    int parent_rank;
    int clique_size;
    int *clique_ranks = NULL;
    const char *child_uri;
    const char *relay_uri;
    int kvsname_len;
    int key_len;
    int val_len;
    char *kvsname = NULL;
    char *key = NULL;
    char *val = NULL;
    int e;
    int rc = -1;
    const char *tbonendpoint = NULL;
    const char *mcastendpoint = NULL;

    if ((e = PMI_Init (&spawned)) != PMI_SUCCESS) {
        log_msg ("PMI_Init: %s", pmi_strerror (e));
        goto done;
    }

    /* Get rank, size, appnum
     */
    if ((e = PMI_Get_size (&size)) != PMI_SUCCESS) {
        log_msg ("PMI_Get_size: %s", pmi_strerror (e));
        goto done;
    }
    if ((e = PMI_Get_rank (&rank)) != PMI_SUCCESS) {
        log_msg ("PMI_Get_rank: %s", pmi_strerror (e));
        goto done;
    }
    if ((e = PMI_Get_appnum (&appnum)) != PMI_SUCCESS) {
        log_msg ("PMI_Get_appnum: %s", pmi_strerror (e));
        goto done;
    }

    overlay_init (overlay, (uint32_t)size, (uint32_t)rank, tbon_k);

    /* Set session-id attribute from PMI appnum if not already set.
     */
    if (attr_get (attrs, "session-id", NULL, NULL) < 0) {
        if (attr_add_int (attrs, "session-id", appnum,
                          FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
    }

    if (update_endpoint_attr (attrs, "tbon.endpoint", &tbonendpoint,
                                                        "tcp://%h:*") < 0)
        goto done;

    overlay_set_child (overlay, tbonendpoint);

    if (update_endpoint_attr (attrs, "mcast.endpoint", &mcastendpoint,
                                                        "tbon") < 0)
        goto done;

    /* Set up multicast (e.g. epgm) relay if multiple ranks are being
     * spawned per node, as indicated by "clique ranks".  FIXME: if
     * pmi_get_clique_ranks() is not implemented, this fails.  Find an
     * alternate method to determine if ranks are co-located on a
     * node.
     */
    if (strcasecmp (mcastendpoint, "tbon")) {
        if ((e = PMI_Get_clique_size (&clique_size)) != PMI_SUCCESS) {
            log_msg ("PMI_get_clique_size: %s", pmi_strerror (e));
            goto done;
        }
        clique_ranks = xzmalloc (sizeof (int) * clique_size);
        if ((e = PMI_Get_clique_ranks (clique_ranks, clique_size))
                                                          != PMI_SUCCESS) {
            log_msg ("PMI_Get_clique_ranks: %s", pmi_strerror (e));
            goto done;
        }
        if (clique_size > 1) {
            int i;
            for (i = 0; i < clique_size; i++)
                if (relay_rank == -1 || clique_ranks[i] < relay_rank)
                    relay_rank = clique_ranks[i];
            if (relay_rank >= 0 && rank == relay_rank) {
                const char *rundir;
                char *relayfile = NULL;

                if (attr_get (attrs, "broker.rundir", &rundir, NULL) < 0) {
                    log_msg ("broker.rundir attribute is not set");
                    goto done;
                }

                relayfile = xasprintf ("%s/relay", rundir);
                overlay_set_relay (overlay, "ipc://%s", relayfile);
                cleanup_push_string (cleanup_file, relayfile);
                free (relayfile);
            }
        }
    }

    /* Prepare for PMI KVS operations by grabbing the kvsname,
     * and buffers for keys and values.
     */
    if ((e = PMI_KVS_Get_name_length_max (&kvsname_len)) != PMI_SUCCESS) {
        log_msg ("PMI_KVS_Get_name_length_max: %s", pmi_strerror (e));
        goto done;
    }
    kvsname = xzmalloc (kvsname_len);
    if ((e = PMI_KVS_Get_my_name (kvsname, kvsname_len)) != PMI_SUCCESS) {
        log_msg ("PMI_KVS_Get_my_name: %s", pmi_strerror (e));
        goto done;
    }
    if ((e = PMI_KVS_Get_key_length_max (&key_len)) != PMI_SUCCESS) {
        log_msg ("PMI_KVS_Get_key_length_max: %s", pmi_strerror (e));
        goto done;
    }
    key = xzmalloc (key_len);
    if ((e = PMI_KVS_Get_value_length_max (&val_len)) != PMI_SUCCESS) {
        log_msg ("PMI_KVS_Get_value_length_max: %s", pmi_strerror (e));
        goto done;
    }
    val = xzmalloc (val_len);

    /* Bind to addresses to expand URI wildcards, so we can exchange
     * the real addresses.
     */
    if (overlay_bind (overlay) < 0) {
        log_err ("overlay_bind failed");   /* function is idempotent */
        goto done;
    }

    /* Write the URI of downstream facing socket under the rank (if any).
     */
    if ((child_uri = overlay_get_child (overlay))) {
        if (snprintf (key, key_len, "cmbd.%d.uri", rank) >= key_len) {
            log_msg ("pmi key string overflow");
            goto done;
        }
        if (snprintf (val, val_len, "%s", child_uri) >= val_len) {
            log_msg ("pmi val string overflow");
            goto done;
        }
        if ((e = PMI_KVS_Put (kvsname, key, val)) != PMI_SUCCESS) {
            log_msg ("PMI_KVS_Put: %s", pmi_strerror (e));
            goto done;
        }
    }

    /* Write the uri of the multicast (e.g. epgm) relay under the rank
     * (if any).
     */
    if (strcasecmp (mcastendpoint, "tbon")
        && (relay_uri = overlay_get_relay (overlay))) {
        if (snprintf (key, key_len, "cmbd.%d.relay", rank) >= key_len) {
            log_msg ("pmi key string overflow");
            goto done;
        }
        if (snprintf (val, val_len, "%s", relay_uri) >= val_len) {
            log_msg ("pmi val string overflow");
            goto done;
        }
        if ((e = PMI_KVS_Put (kvsname, key, val)) != PMI_SUCCESS) {
            log_msg ("PMI_KVS_Put: %s", pmi_strerror (e));
            goto done;
        }
    }

    /* Puts are complete, now we synchronize and begin our gets.
     */
    if ((e = PMI_KVS_Commit (kvsname)) != PMI_SUCCESS) {
        log_msg ("PMI_KVS_Commit: %s", pmi_strerror (e));
        goto done;
    }
    if ((e = PMI_Barrier ()) != PMI_SUCCESS) {
        log_msg ("PMI_Barrier: %s", pmi_strerror (e));
        goto done;
    }

    /* Read the uri of our parent, after computing its rank
     */
    if (rank > 0) {
        parent_rank = kary_parentof (tbon_k, (uint32_t)rank);
        if (snprintf (key, key_len, "cmbd.%d.uri", parent_rank) >= key_len) {
            log_msg ("pmi key string overflow");
            goto done;
        }
        if ((e = PMI_KVS_Get (kvsname, key, val, val_len)) != PMI_SUCCESS) {
            log_msg ("pmi_kvs_get: %s", pmi_strerror (e));
            goto done;
        }
        overlay_set_parent (overlay, "%s", val);
    }

    /* Event distribution (four configurations):
     * 1) multicast enabled, one broker per node
     *    All brokers subscribe to the same epgm address.
     * 2) multicast enabled, mutiple brokers per node The lowest rank
     *    in each clique will subscribe to the multicast
     *    (e.g. epgm://) socket and relay events to an ipc:// socket
     *    for the other ranks in the clique.  This is necessary due to
     *    limitation of epgm.
     * 3) multicast disabled, all brokers concentrated on one node
     *    Rank 0 publishes to a ipc:// socket, other ranks subscribe (set earlier via mcast.endpoint)
     * 4) multicast disabled brokers distributed across nodes
     *    No dedicated event overlay,.  Events are distributed over the TBON.
     */
    if (strcasecmp (mcastendpoint, "tbon")) {
        if (relay_rank >= 0 && rank != relay_rank) {
            if (snprintf (key, key_len, "cmbd.%d.relay", relay_rank)
                                                                >= key_len) {
                log_msg ("pmi key string overflow");
                goto done;
            }
            if ((e = PMI_KVS_Get (kvsname, key, val, val_len))
                                                            != PMI_SUCCESS) {
                log_msg ("PMI_KVS_Get: %s", pmi_strerror (e));
                goto done;
            }
            overlay_set_event (overlay, "%s", val);
        } else
            overlay_set_event (overlay, mcastendpoint);
    }
    if ((e = PMI_Barrier ()) != PMI_SUCCESS) {
        log_msg ("PMI_Barrier: %s", pmi_strerror (e));
        goto done;
    }
    PMI_Finalize ();
    rc = 0;
done:
    if (clique_ranks)
        free (clique_ranks);
    if (kvsname)
        free (kvsname);
    if (key)
        free (key);
    if (val)
        free (val);
    if (rc != 0)
        errno = EPROTO;
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
