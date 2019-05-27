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
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/param.h>

#include "simple_client.h"
#include "simple_server.h"
#include "dgetline.h"
#include "keyval.h"
#include "pmi.h"

int pmi_simple_client_init (struct pmi_simple_client *pmi)
{
    int result = PMI_FAIL;
    unsigned int vers, subvers;
    char buf[SIMPLE_MAX_PROTO_LINE];
    int rc;

    if (!pmi)
        return PMI_ERR_INIT;
    if (dprintf (pmi->fd, "cmd=init pmi_version=1 pmi_subversion=1\n") < 0)
        goto done;
    if (dgetline (pmi->fd, buf, sizeof (buf)) < 0)
        goto done;
    if (keyval_parse_isword (buf, "cmd", "response_to_init") < 0)
        goto done;
    if (keyval_parse_int (buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    if (keyval_parse_uint (buf, "pmi_version", &vers) < 0
            || keyval_parse_uint (buf, "pmi_subversion", &subvers) < 0)
        goto done;
    if (vers != 1 || subvers != 1)
        goto done;

    if (dprintf (pmi->fd, "cmd=get_maxes\n") < 0)
        goto done;
    if (dgetline (pmi->fd, buf, sizeof (buf)) < 0)
        goto done;
    if (keyval_parse_isword (buf, "cmd", "maxes") < 0)
        goto done;
    if (keyval_parse_int (buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    if (keyval_parse_uint (buf, "kvsname_max", &pmi->kvsname_max) < 0
            || keyval_parse_uint (buf, "keylen_max", &pmi->keylen_max) < 0
            || keyval_parse_uint (buf, "vallen_max", &pmi->vallen_max) < 0)
        goto done;
    pmi->buflen = pmi->keylen_max + pmi->vallen_max + pmi->kvsname_max
                                  + SIMPLE_MAX_PROTO_OVERHEAD;
    if (!(pmi->buf = calloc (1, pmi->buflen))) {
        result = PMI_ERR_NOMEM;
        goto done;
    }
    pmi->initialized = 1;
    result = PMI_SUCCESS;
done:
    return result;
}

int pmi_simple_client_finalize (struct pmi_simple_client *pmi)
{
    int result = PMI_FAIL;
    int rc;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    if (dprintf (pmi->fd, "cmd=finalize\n") < 0)
        goto done;
    if (dgetline (pmi->fd, pmi->buf, pmi->buflen) < 0)
        goto done;
    if (keyval_parse_isword (pmi->buf, "cmd", "finalize_ack") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    result = PMI_SUCCESS;
done:
    return result;
}

int pmi_simple_client_get_appnum (struct pmi_simple_client *pmi, int *appnum)
{
    int result = PMI_FAIL;
    int rc;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    if (dprintf (pmi->fd, "cmd=get_appnum\n") < 0)
        goto done;
    if (dgetline (pmi->fd, pmi->buf, pmi->buflen) < 0)
        goto done;
    if (keyval_parse_isword (pmi->buf, "cmd", "appnum") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    if (keyval_parse_int (pmi->buf, "appnum", appnum) < 0)
        goto done;
    result = PMI_SUCCESS;
done:
    return result;
}

int pmi_simple_client_get_universe_size (struct pmi_simple_client *pmi,
                                         int *universe_size)
{
    int result = PMI_FAIL;
    int rc;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    if (dprintf (pmi->fd, "cmd=get_universe_size\n") < 0)
        goto done;
    if (dgetline (pmi->fd, pmi->buf, pmi->buflen) < 0)
        goto done;
    if (keyval_parse_isword (pmi->buf, "cmd", "universe_size") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    if (keyval_parse_int (pmi->buf, "size", universe_size) < 0)
        goto done;
    result = PMI_SUCCESS;
done:
    return result;
}

int pmi_simple_client_barrier (struct pmi_simple_client *pmi)
{
    int result = PMI_FAIL;
    int rc;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    if (dprintf (pmi->fd, "cmd=barrier_in\n") < 0)
        goto done;
    if (dgetline (pmi->fd, pmi->buf, pmi->buflen) < 0)
        goto done;
    if (keyval_parse_isword (pmi->buf, "cmd", "barrier_out") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    result = PMI_SUCCESS;
done:
    return result;
}

int pmi_simple_client_kvs_get_my_name (struct pmi_simple_client *pmi,
                                       char *kvsname,
                                       int length)
{
    int result = PMI_FAIL;
    int rc;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    if (dprintf (pmi->fd, "cmd=get_my_kvsname\n") < 0)
        goto done;
    if (dgetline (pmi->fd, pmi->buf, pmi->buflen) < 0)
        goto done;
    if (keyval_parse_isword (pmi->buf, "cmd", "my_kvsname") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    if (keyval_parse_word (pmi->buf, "kvsname", kvsname, length) < 0)
        goto done;
    result = PMI_SUCCESS;
done:
    return result;
}

int pmi_simple_client_kvs_put (struct pmi_simple_client *pmi,
                               const char *kvsname,
                               const char *key,
                               const char *value)
{
    int result = PMI_FAIL;
    int rc;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    if (dprintf (pmi->fd, "cmd=put kvsname=%s key=%s value=%s\n",
                 kvsname, key, value) < 0)
        goto done;
    if (dgetline (pmi->fd, pmi->buf, pmi->buflen) < 0)
        goto done;
    if (keyval_parse_isword (pmi->buf, "cmd", "put_result") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    result = PMI_SUCCESS;
done:
    return result;
}

int pmi_simple_client_kvs_get (struct pmi_simple_client *pmi,
                               const char *kvsname,
                               const char *key,
                               char *value,
                               int len)
{
    int result = PMI_FAIL;
    int rc;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    if (dprintf (pmi->fd, "cmd=get kvsname=%s key=%s\n", kvsname, key) < 0)
        goto done;
    if (dgetline (pmi->fd, pmi->buf, pmi->buflen) < 0)
        goto done;
    if (keyval_parse_isword (pmi->buf, "cmd", "get_result") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    if (keyval_parse_string (pmi->buf, "value", value, len) < 0)
        goto done;
    result = PMI_SUCCESS;
done:
    return result;
}

void pmi_simple_client_destroy (struct pmi_simple_client *pmi)
{
    if (pmi) {
        int saved_errno = errno;
        if (pmi->fd != -1)
            (void)close (pmi->fd);
        free (pmi->buf);
        free (pmi);
        errno = saved_errno;
    }
}

struct pmi_simple_client *pmi_simple_client_create_fd (const char *pmi_fd,
                                                       const char *pmi_rank,
                                                       const char *pmi_size,
                                                       const char *pmi_debug,
                                                       const char *pmi_spawned)
{
    struct pmi_simple_client *pmi;

    if (!pmi_fd || !pmi_rank || !pmi_size) {
        errno = EINVAL;
        return NULL;
    }
    if (!(pmi = calloc (1, sizeof (*pmi))))
        return NULL;
    pmi->fd = strtol (pmi_fd, NULL, 10);
    pmi->rank = strtol (pmi_rank, NULL, 10);
    pmi->size = strtol (pmi_size, NULL, 10);
    if (pmi->fd < 0 || pmi->rank < 0 || pmi->size < 1) {
        pmi_simple_client_destroy (pmi);
        errno = EINVAL;
        return NULL;
    }
    if (pmi_spawned)
        pmi->spawned = strtol (pmi_spawned, NULL, 10);
    if (pmi_debug)
        pmi->debug = strtol (pmi_debug, NULL, 10);
    return pmi;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
