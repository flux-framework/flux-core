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
    int parent_rank;
    const char *child_uri;
    char key[64];
    char val[1024];
    const char *tbonendpoint = NULL;
    struct pmi_handle *pmi;
    struct pmi_params pmi_params;
    int result;

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

    /* If there are to be downstream peers, then bind to socket and share the
     * concretized URI with other ranks via PMI KVS key=cmbd.<rank>.uri.
     * N.B. there are no downstream peers if the 0th child of this rank
     * in k-ary tree does not exist.
     */
    if (kary_childof (tbon_k,
                      pmi_params.size,
                      pmi_params.rank,
                      0) != KARY_NONE) {

        if (update_endpoint_attr (attrs,
                                  "tbon.endpoint",
                                  &tbonendpoint,
                                  "tcp://%h:*") < 0) {
            log_msg ("update_endpoint_attr failed");
            goto error;
        }
        if (overlay_set_child (overlay, tbonendpoint) < 0) {
            log_err ("overlay_set_child");
            goto error;
        }
        if (overlay_bind (overlay) < 0) {
            log_err ("overlay_bind failed");   /* function is idempotent */
            goto error;
        }
        if (!(child_uri = overlay_get_child (overlay))) {
            log_msg ("overlay_get_child returned NULL");
            goto error;
        }
        if (snprintf (key, sizeof (key),
                      "cmbd.%d.uri", pmi_params.rank) >= sizeof (key)) {
            log_msg ("pmi key string overflow");
            goto error;
        }
        if (snprintf (val, sizeof (val), "%s", child_uri) >= sizeof (val)) {
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
    }
    else {
        (void)attr_delete (attrs, "tbon.endpoint", true);
        if (attr_add (attrs,
                     "tbon.endpoint",
                     NULL,
                     FLUX_ATTRFLAG_IMMUTABLE) < 0) {
            log_err ("setattr tbon.endpoint");
            goto error;
        }
    }

    /* The PMI barrier (which is implicitly over 'size' ranks) ensures that
     * all KVS puts are complete before any PMI gets.
     */
    result = broker_pmi_barrier (pmi);
    if (result != PMI_SUCCESS) {
        log_msg ("broker_pmi_barrier: %s", pmi_strerror (result));
        goto error;
    }

    /* If there is to be an upstream peer, fetch its URI from PMI KVS.
     * N.B. only rank 0 has no upstream peer.
     */
    if (pmi_params.rank > 0) {
        parent_rank = kary_parentof (tbon_k, pmi_params.rank);
        if (snprintf (key, sizeof (key),
                      "cmbd.%d.uri", parent_rank) >= sizeof (key)) {
            log_msg ("pmi key string overflow");
            goto error;
        }
        result = broker_pmi_kvs_get (pmi, pmi_params.kvsname,
                                     key, val, sizeof (val));
        if (result != PMI_SUCCESS) {
            log_msg ("broker_pmi_kvs_get: %s", pmi_strerror (result));
            goto error;
        }
        if (overlay_set_parent (overlay, "%s", val) < 0) {
            log_err ("overlay_set_parent");
            goto error;
        }
    }

    result = broker_pmi_barrier (pmi);
    if (result != PMI_SUCCESS) {
        log_msg ("broker_pmi_barrier: %s", pmi_strerror (result));
        goto error;
    }

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
