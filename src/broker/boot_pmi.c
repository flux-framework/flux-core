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
#include <assert.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/ipaddr.h"
#include "src/common/libutil/kary.h"
#include "src/common/libpmi/pmi.h"
#include "src/common/libpmi/pmi_strerror.h"
#include "src/common/libpmi/clique.h"

#include "attr.h"
#include "overlay.h"
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

int boot_pmi (struct overlay *overlay, attr_t *attrs, int tbon_k)
{
    int rank;
    char key[64];
    char val[1024];
    struct pmi_handle *pmi;
    struct pmi_params pmi_params;
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
    if (overlay_set_geometry (overlay,
                              pmi_params.size,
                              pmi_params.rank,
                              tbon_k) < 0)
        goto error;

    /* A size=1 instance has no peers, so skip the PMI exchange.
     */
    if (pmi_params.size == 1)
        goto done;

    /* If there are to be downstream peers, then bind to socket and extract
     * the concretized URI for sharing with other ranks.
     * N.B. there are no downstream peers if the 0th child of this rank
     * in k-ary tree does not exist.
     */
    if (kary_childof (tbon_k,
                      pmi_params.size,
                      pmi_params.rank,
                      0) != KARY_NONE) {
        char buf[1024];

        if (format_bind_uri (buf, sizeof (buf), attrs, pmi_params.rank) < 0)
            goto error;
        if (overlay_bind (overlay, buf) < 0) {
            log_err ("error binding to %s", buf);
            goto error;
        }
        uri = overlay_get_bind_uri (overlay);
    }
    else {
        uri = NULL;
    }
    if (attr_add (attrs, "tbon.endpoint", uri, FLUX_ATTRFLAG_IMMUTABLE) < 0) {
        log_err ("setattr tbon.endpoint");
        goto error;
    }

    /* Each broker writes a "business card" consisting of (currently):
     * pubkey[,URI].  The URI and separator are omitted if broker is
     * a leaf in the TBON and won't be creating its own endpoint.
     */
    if (snprintf (key, sizeof (key), "%d", pmi_params.rank) >= sizeof (key)) {
        log_msg ("pmi key string overflow");
        goto error;
    }
    if (snprintf (val,
                  sizeof (val),
                  "%s%s%s",
                  overlay_cert_pubkey (overlay),
                  uri ? "," : "",
                  uri ? uri : "") >= sizeof (val)) {
        log_msg ("pmi val string overflow");
        goto error;
    }
    result = broker_pmi_kvs_put (pmi, pmi_params.kvsname, key, val);
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
        char *cp;

        rank = kary_parentof (tbon_k, pmi_params.rank);
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
        if ((cp = strchr (val, ',')))
            *cp++ = '\0';
        if (!cp) {
            log_msg ("rank %d business card has no URI", rank);
            goto error;
        }
        if (overlay_set_parent_uri (overlay, cp) < 0) {
            log_err ("overlay_set_parent_uri");
            goto error;
        }
        if (overlay_set_parent_pubkey (overlay, val) < 0) {
            log_err ("overlay_set_parent_pubkey");
            goto error;
        }
    }

    /* Fetch the business card of children and inform overlay of public keys.
     */
    for (i = 0; i < tbon_k; i++) {
        char *cp;

        rank = kary_childof (tbon_k, pmi_params.size, pmi_params.rank, i);
        if (rank == KARY_NONE)
            break;
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
        if ((cp = strchr (val, ',')))
            *cp = '\0';
        if (overlay_authorize (overlay, key, val) < 0) {
            log_err ("overlay_authorize %s=%s", key, val);
            goto error;
        }
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

    broker_pmi_destroy (pmi);
    return 0;
error:
    broker_pmi_destroy (pmi);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
