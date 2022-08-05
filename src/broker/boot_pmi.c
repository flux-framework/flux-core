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
#include <sys/param.h>
#include <unistd.h>
#include <jansson.h>
#include <assert.h>
#include <flux/hostlist.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/ipaddr.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libpmi/pmi.h"
#include "src/common/libpmi/pmi_strerror.h"
#include "src/common/libpmi/clique.h"

#include "attr.h"
#include "overlay.h"
#include "topology.h"
#include "boot_pmi.h"
#include "pmiutil.h"


/*  If the broker is launched via flux-shell, then the shell may opt
 *  to set a "flux.instance-level" parameter in the PMI kvs to tell
 *  the booting instance at what "level" it will be running, i.e. the
 *  number of parents. If the PMI key is missing, this is not an error,
 *  instead the level of this instance is considered to be zero.
 *  Additonally, if level > 0, the shell will have put the instance's
 *  jobid in the PMI kvsname for us as well, so populate the 'jobid' attr.
 */
static int set_instance_level_attr (struct pmi_handle *pmi,
                                    const char *kvsname,
                                    attr_t *attrs)
{
    int result;
    char val[32];
    const char *level = "0";
    const char *jobid = NULL;

    result = broker_pmi_kvs_get (pmi,
                                 kvsname,
                                 "flux.instance-level",
                                 val,
                                 sizeof (val),
                                 -1);
    if (result == PMI_SUCCESS)
        level = val;
    if (attr_add (attrs, "instance-level", level, FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    if (result == PMI_SUCCESS)
        jobid = kvsname;
    if (attr_add (attrs, "jobid", jobid, FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    return 0;
}

/* Set broker.mapping attribute from enclosing instance PMI_process_mapping.
 */
static int set_broker_mapping_attr (struct pmi_handle *pmi,
                                    struct pmi_params *pmi_params,
                                    attr_t *attrs)
{
    char buf[1024];
    char *val = NULL;

    if (pmi_params->size == 1)
        val = "(vector,(0,1,1))";
    else {
        if (broker_pmi_kvs_get (pmi,
                                pmi_params->kvsname,
                                "PMI_process_mapping",
                                buf,
                                sizeof (buf),
                                -1) == PMI_SUCCESS)
            val = buf;
    }
    if (attr_add (attrs, "broker.mapping", val, FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    return 0;
}

/* Check if IPC can be used to communicate.
 * Currently this only goes so far as to check if the process mapping of
 * brokers has all brokers on the same node.  We could check if all peers
 * are on the same node, but given how the TBON maps to rank assignments,
 * it is fairly unlikely.
 */
static bool use_ipc (attr_t *attrs)
{
    bool result = false;
    struct pmi_map_block *blocks = NULL;
    int nblocks;
    const char *val;

    if (attr_get (attrs, "tbon.prefertcp", &val, NULL) == 0
        && strcmp (val, "0") != 0)
        goto done;
    if (attr_get (attrs, "broker.mapping", &val, NULL) < 0 || !val)
        goto done;
    if (pmi_process_mapping_parse (val, &blocks, &nblocks) < 0)
        goto done;
    if (nblocks == 1 && blocks[0].nodes == 1) // one node
        result = true;
done:
    free (blocks);
    return result;
}

/* Build URI for broker TBON to bind to.
 * If IPC, use '<rundir>/tbon-<rank>' which should be unique if there are
 * multiple brokers and/or multiple instances per node.
 * If using TCP, choose the address to be the one associated with the default
 * route (see src/common/libutil/ipaddr.h), and a randomly chosen port.
 */
static int format_bind_uri (char *buf, int bufsz, attr_t *attrs, int rank)
{
    if (use_ipc (attrs)) {
        const char *rundir;

        if (attr_get (attrs, "rundir", &rundir, NULL) < 0) {
            log_err ("rundir attribute is not set");
            return -1;
        }
        if (snprintf (buf, bufsz, "ipc://%s/tbon-%d", rundir, rank) >= bufsz)
            goto overflow;
    }
    else {
        char ipaddr[HOST_NAME_MAX + 1];
        char error[200];

        if (ipaddr_getprimary (ipaddr, sizeof (ipaddr),
                               error, sizeof (error)) < 0) {
            log_err ("%s", error);
            return -1;
        }
        if (snprintf (buf, bufsz, "tcp://%s:*", ipaddr) >= bufsz)
            goto overflow;
    }
    return 0;
overflow:
    log_msg ("buffer overflow while building bind URI");
    return -1;
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
    if (s && attr_add (attrs, "hostlist", s, FLUX_ATTRFLAG_IMMUTABLE) == 0)
        rc = 0;
    ERRNO_SAFE_WRAP (free, s);
    return rc;
}

int boot_pmi (struct overlay *overlay, attr_t *attrs)
{
    int fanout = overlay_get_fanout (overlay);
    char key[64];
    char val[1024];
    char hostname[MAXHOSTNAMELEN + 1];
    char *bizcard = NULL;
    struct hostlist *hl = NULL;
    json_t *o;
    struct pmi_handle *pmi;
    struct pmi_params pmi_params;
    struct topology *topo = NULL;
    int child_count;
    int *child_ranks = NULL;
    int result;
    const char *uri;
    int i;

    memset (&pmi_params, 0, sizeof (pmi_params));
    if (!(pmi = broker_pmi_create ())) {
        log_err ("broker_pmi_create");
        goto error;
    }
    result = broker_pmi_init (pmi);
    if (result != PMI_SUCCESS) {
        log_msg ("broker_pmi_init: %s", pmi_strerror (result));
        goto error;
    }
    result = broker_pmi_get_params (pmi, &pmi_params);
    if (result != PMI_SUCCESS) {
        log_msg ("broker_pmi_get_params: %s", pmi_strerror (result));
        goto error;
    }
    if (set_instance_level_attr (pmi, pmi_params.kvsname, attrs) < 0) {
        log_err ("set_instance_level_attr");
        goto error;
    }
    if (set_broker_mapping_attr (pmi, &pmi_params, attrs) < 0) {
        log_err ("error setting broker.mapping attribute");
        goto error;
    }
    if (!(topo = topology_create (pmi_params.size))
        || topology_set_kary (topo, fanout) < 0
        || topology_set_rank (topo, pmi_params.rank) < 0
        || overlay_set_topology (overlay, topo) < 0)
        goto error;
    if (gethostname (hostname, sizeof (hostname)) < 0) {
        log_err ("gethostname");
        goto error;
    }
    if (!(hl = hostlist_create ())) {
        log_err ("hostlist_create");
        goto error;
    }

    /* A size=1 instance has no peers, so skip the PMI exchange.
     */
    if (pmi_params.size == 1) {
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

    /* If there are to be downstream peers, then bind to socket and extract
     * the concretized URI for sharing with other ranks.
     */
    if (child_count > 0) {
        char buf[1024];

        if (format_bind_uri (buf, sizeof (buf), attrs, pmi_params.rank) < 0)
            goto error;
        if (overlay_bind (overlay, buf) < 0)
            goto error;
        uri = overlay_get_bind_uri (overlay);
    }
    else {
        uri = NULL;
    }
    if (attr_add (attrs, "tbon.endpoint", uri, FLUX_ATTRFLAG_IMMUTABLE) < 0) {
        log_err ("setattr tbon.endpoint");
        goto error;
    }

    /* Each broker writes a "business card" consisting of hostname,
     * public key, and URI (empty string for leaf node).
     */
    if (snprintf (key, sizeof (key), "%d", pmi_params.rank) >= sizeof (key)) {
        log_msg ("pmi key string overflow");
        goto error;
    }
    if (!(o = json_pack ("{s:s s:s s:s}",
                         "hostname", hostname,
                         "pubkey", overlay_cert_pubkey (overlay),
                         "uri", uri ? uri : ""))
        || !(bizcard = json_dumps (o, JSON_COMPACT))) {
        log_msg ("error encoding pmi business card object");
        json_decref (o);
        goto error;
    }
    json_decref (o);
    result = broker_pmi_kvs_put (pmi, pmi_params.kvsname, key, bizcard);
    if (result != PMI_SUCCESS) {
        log_msg ("broker_pmi_kvs_put: %s", pmi_strerror (result));
        goto error;
    }
    result = broker_pmi_kvs_commit (pmi, pmi_params.kvsname);
    if (result != PMI_SUCCESS) {
        log_msg ("broker_pmi_kvs_commit: %s", pmi_strerror (result));
        goto error;
    }
    result = broker_pmi_barrier (pmi);
    if (result != PMI_SUCCESS) {
        log_msg ("broker_pmi_barrier: %s", pmi_strerror (result));
        goto error;
    }

    /* Fetch the business card of parent and inform overlay of URI
     * and public key.
     */
    if (pmi_params.rank > 0) {
        const char *peer_pubkey;
        const char *peer_uri;
        int rank = topology_get_parent (topo);

        if (snprintf (key, sizeof (key), "%d", rank) >= sizeof (key)) {
            log_msg ("pmi key string overflow");
            goto error;
        }
        result = broker_pmi_kvs_get (pmi, pmi_params.kvsname,
                                     key, val, sizeof (val), rank);
        if (result != PMI_SUCCESS) {
            log_msg ("broker_pmi_kvs_get %s: %s", key, pmi_strerror (result));
            goto error;
        }
        if (!(o = json_loads (val, 0, NULL))
            || json_unpack (o,
                            "{s:s s:s}",
                            "pubkey", &peer_pubkey,
                            "uri", &peer_uri) < 0
            || strlen (peer_uri) == 0) {
            log_msg ("error decoding rank %d business card", rank);
            json_decref (o);
            goto error;
        }
        if (overlay_set_parent_uri (overlay, peer_uri) < 0) {
            log_err ("overlay_set_parent_uri");
            json_decref (o);
            goto error;
        }
        if (overlay_set_parent_pubkey (overlay, peer_pubkey) < 0) {
            log_err ("overlay_set_parent_pubkey");
            json_decref (o);
            goto error;
        }
        json_decref (o);
    }

    /* Fetch the business card of children and inform overlay of public keys.
     */
    for (i = 0; i < child_count; i++) {
        const char *peer_pubkey;
        int rank = child_ranks[i];

        if (snprintf (key, sizeof (key), "%d", rank) >= sizeof (key)) {
            log_msg ("pmi key string overflow");
            goto error;
        }
        result = broker_pmi_kvs_get (pmi, pmi_params.kvsname,
                                     key, val, sizeof (val), rank);

        if (result != PMI_SUCCESS) {
            log_msg ("broker_pmi_kvs_get %s: %s", key, pmi_strerror (result));
            goto error;
        }
        if (!(o = json_loads (val, 0, NULL))
            || json_unpack (o, "{s:s}", "pubkey", &peer_pubkey) < 0) {
            log_msg ("error decoding rank %d business card", rank);
            json_decref (o);
            goto error;
        }
        if (overlay_authorize (overlay, key, peer_pubkey) < 0) {
            log_err ("overlay_authorize %s=%s", key, peer_pubkey);
            json_decref (o);
            goto error;
        }
        json_decref (o);
    }

    /* Fetch the business card of all ranks and build hostlist.
     * The hostlist is built indepenedently (and in parallel) on all ranks.
     */
    for (i = 0; i < pmi_params.size; i++) {
        const char *peer_hostname;

        if (snprintf (key, sizeof (key), "%d", i) >= sizeof (key)) {
            log_msg ("pmi key string overflow");
            goto error;
        }
        result = broker_pmi_kvs_get (pmi, pmi_params.kvsname,
                                     key, val, sizeof (val), i);

        if (result != PMI_SUCCESS) {
            log_msg ("broker_pmi_kvs_get %s: %s", key, pmi_strerror (result));
            goto error;
        }
        if (!(o = json_loads (val, 0, NULL))
            || json_unpack (o, "{s:s}", "hostname", &peer_hostname) < 0) {
            log_msg ("error decoding rank %d pmi business card", i);
            json_decref (o);
            goto error;
        }
        if (hostlist_append (hl, peer_hostname) < 0) {
            log_err ("hostlist_append");
            json_decref (o);
            goto error;
        }
        json_decref (o);
    }

    /* One more barrier before allowing connects to commence.
     * Need to ensure that all clients are "allowed".
     */
    result = broker_pmi_barrier (pmi);
    if (result != PMI_SUCCESS) {
        log_msg ("broker_pmi_barrier: %s", pmi_strerror (result));
        goto error;
    }

done:
    result = broker_pmi_finalize (pmi);
    if (result != PMI_SUCCESS) {
        log_msg ("broker_pmi_finalize: %s", pmi_strerror (result));
        goto error;
    }
    if (set_hostlist_attr (attrs, hl) < 0) {
        log_err ("failed to set hostlist attribute to PMI-derived value");
        goto error;
    }
    free (bizcard);
    broker_pmi_destroy (pmi);
    hostlist_destroy (hl);
    free (child_ranks);
    topology_decref (topo);
    return 0;
error:
    free (bizcard);
    broker_pmi_destroy (pmi);
    hostlist_destroy (hl);
    free (child_ranks);
    topology_decref (topo);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
