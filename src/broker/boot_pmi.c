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

#include "attr.h"
#include "overlay.h"
#include "boot_pmi.h"
#include "pmiutil.h"


/* Generally accepted max, although some go higher (IE is 2083) */
#define ENDPOINT_MAX 2048

/* Given a string with possible format specifiers, return string that is
 * fully expanded.
 *
 * Possible format specifiers:
 * - %h - local IP address by heuristic (see src/libutil/ipaddr.h)
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

    if (!(buf = calloc (1, ENDPOINT_MAX + 1))) {
        errno = ENOMEM;
        return NULL;
    }

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
                                 sizeof (val));
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
    if (overlay_init (overlay, pmi_params.size, pmi_params.rank, tbon_k) < 0)
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
        const char *fmt;
        char *tmp;

        if (attr_get (attrs, "tbon.endpoint", &fmt, NULL) < 0)
            fmt = "tcp://%h:*";
        if (!(tmp = format_endpoint (attrs, fmt)))
            goto error;
        if (overlay_bind (overlay, tmp) < 0) {
            log_err ("overlay_bind %s failed", tmp);
            free (tmp);
            goto error;
        }
        free (tmp);
        uri = overlay_get_bind_uri (overlay);
    }
    else {
        uri = NULL;
    }
    (void)attr_delete (attrs, "tbon.endpoint", true);
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
                                     key, val, sizeof (val));
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
                                     key, val, sizeof (val));

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
