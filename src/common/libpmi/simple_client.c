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

struct pmi_simple_client {
    int fd;
    int rank;
    int size;
    int spawned;
    int initialized;
    unsigned int kvsname_max;
    unsigned int keylen_max;
    unsigned int vallen_max;
    char *buf;
    int buflen;
};

static int pmi_simple_client_init (void *impl, int *spawned)
{
    struct pmi_simple_client *pmi = impl;
    int result = PMI_FAIL;
    unsigned int vers, subvers;
    char buf[SIMPLE_MAX_PROTO_LINE];
    int rc;

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
    if (spawned)
        *spawned = pmi->spawned;
    result = PMI_SUCCESS;
done:
    return result;
}

static int pmi_simple_client_initialized (void *impl, int *initialized)
{
    struct pmi_simple_client *pmi = impl;
    *initialized = pmi->initialized;
    return PMI_SUCCESS;
}

static int pmi_simple_client_finalize (void *impl)
{
    struct pmi_simple_client *pmi = impl;
    int result = PMI_FAIL;
    int rc;

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

static int pmi_simple_client_get_size (void *impl, int *size)
{
    struct pmi_simple_client *pmi = impl;
    if (!pmi->initialized)
        return PMI_FAIL;
    *size = pmi->size;
    return PMI_SUCCESS;
}

static int pmi_simple_client_get_rank (void *impl, int *rank)
{
    struct pmi_simple_client *pmi = impl;
    if (!pmi->initialized)
        return PMI_FAIL;
    *rank = pmi->rank;
    return PMI_SUCCESS;
}

static int pmi_simple_client_get_appnum (void *impl, int *appnum)
{
    struct pmi_simple_client *pmi = impl;
    int result = PMI_FAIL;
    int rc;

    if (!pmi->initialized)
        goto done;
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

static int pmi_simple_client_get_universe_size (void *impl, int *universe_size)
{
    struct pmi_simple_client *pmi = impl;
    int result = PMI_FAIL;
    int rc;

    if (!pmi->initialized)
        goto done;
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

static int pmi_simple_client_publish_name (void *impl,
                                           const char *service_name,
                                           const char *port)
{
    return PMI_FAIL;
}

static int pmi_simple_client_unpublish_name (void *impl, const char *service_name)
{
    return PMI_FAIL;
}

static int pmi_simple_client_lookup_name (void *impl,
                                          const char *service_name,
                                          char *port)
{
    return PMI_FAIL;
}

static int pmi_simple_client_barrier (void *impl)
{
    struct pmi_simple_client *pmi = impl;
    int result = PMI_FAIL;
    int rc;

    if (!pmi->initialized)
        goto done;
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

static int pmi_simple_client_abort (void *impl, int exit_code, const char *error_msg)
{
    fprintf (stderr, "PMI_Abort: %s\n", error_msg);
    exit (exit_code);
    /*NOTREACHED*/
    return PMI_SUCCESS;
}

static int pmi_simple_client_kvs_get_my_name (void *impl, char *kvsname, int length)
{
    struct pmi_simple_client *pmi = impl;
    int result = PMI_FAIL;
    int rc;

    if (!pmi->initialized)
        goto done;
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

static int pmi_simple_client_kvs_get_name_length_max (void *impl, int *length)
{
    struct pmi_simple_client *pmi = impl;
    if (!pmi->initialized)
        return PMI_FAIL;
    *length = pmi->kvsname_max;
    return PMI_SUCCESS;
}

static int pmi_simple_client_kvs_get_key_length_max (void *impl, int *length)
{
    struct pmi_simple_client *pmi = impl;
    if (!pmi->initialized)
        return PMI_FAIL;
    *length = pmi->keylen_max;
    return PMI_SUCCESS;
}

static int pmi_simple_client_kvs_get_value_length_max (void *impl, int *length)
{
    struct pmi_simple_client *pmi = impl;
    if (!pmi->initialized)
        return PMI_FAIL;
    *length = pmi->vallen_max;
    return PMI_SUCCESS;
}

static int pmi_simple_client_kvs_put (void *impl,
                                      const char *kvsname,
                                      const char *key,
                                      const char *value)
{
    struct pmi_simple_client *pmi = impl;
    int result = PMI_FAIL;
    int rc;

    if (!pmi->initialized)
        goto done;
    if (dprintf (pmi->fd, "cmd=put kvsname=%s key=%s value=%s\n", kvsname, key, value)
        < 0)
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

static int pmi_simple_client_kvs_commit (void *impl, const char *kvsname)
{
    return PMI_SUCCESS; /* a no-op here */
}

static int pmi_simple_client_kvs_get (void *impl,
                                      const char *kvsname,
                                      const char *key,
                                      char *value,
                                      int len)
{
    struct pmi_simple_client *pmi = impl;
    int result = PMI_FAIL;
    int rc;

    if (!pmi->initialized)
        goto done;
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

static int pmi_simple_client_spawn_multiple (void *impl,
                                             int count,
                                             const char *cmds[],
                                             const char **argvs[],
                                             const int maxprocs[],
                                             const int info_keyval_sizesp[],
                                             const PMI_keyval_t *info_keyval_vectors[],
                                             int preput_keyval_size,
                                             const PMI_keyval_t preput_keyval_vector[],
                                             int errors[])
{
    return PMI_FAIL;
}

static void pmi_simple_client_destroy (void *impl)
{
    struct pmi_simple_client *pmi = impl;
    if (pmi) {
        if (pmi->fd != -1)
            (void)close (pmi->fd);
        if (pmi->buf)
            free (pmi->buf);
        free (pmi);
    }
}

static struct pmi_operations pmi_simple_operations = {
    .init = pmi_simple_client_init,
    .initialized = pmi_simple_client_initialized,
    .finalize = pmi_simple_client_finalize,
    .get_size = pmi_simple_client_get_size,
    .get_rank = pmi_simple_client_get_rank,
    .get_appnum = pmi_simple_client_get_appnum,
    .get_universe_size = pmi_simple_client_get_universe_size,
    .publish_name = pmi_simple_client_publish_name,
    .unpublish_name = pmi_simple_client_unpublish_name,
    .lookup_name = pmi_simple_client_lookup_name,
    .barrier = pmi_simple_client_barrier,
    .abort = pmi_simple_client_abort,
    .kvs_get_my_name = pmi_simple_client_kvs_get_my_name,
    .kvs_get_name_length_max = pmi_simple_client_kvs_get_name_length_max,
    .kvs_get_key_length_max = pmi_simple_client_kvs_get_key_length_max,
    .kvs_get_value_length_max = pmi_simple_client_kvs_get_value_length_max,
    .kvs_put = pmi_simple_client_kvs_put,
    .kvs_commit = pmi_simple_client_kvs_commit,
    .kvs_get = pmi_simple_client_kvs_get,
    .get_clique_size = NULL,
    .get_clique_ranks = NULL,
    .spawn_multiple = pmi_simple_client_spawn_multiple,
    .destroy = pmi_simple_client_destroy,
};

void *pmi_simple_client_create (struct pmi_operations **ops)
{
    struct pmi_simple_client *pmi = calloc (1, sizeof (*pmi));
    const char *s;

    if (!pmi)
        return NULL;
    if (!(s = getenv ("PMI_FD")))
        goto error;
    pmi->fd = strtol (s, NULL, 10);
    if (!(s = getenv ("PMI_RANK")))
        goto error;
    pmi->rank = strtol (s, NULL, 10);
    if (!(s = getenv ("PMI_SIZE")))
        goto error;
    pmi->size = strtol (s, NULL, 10);
    pmi->spawned = 0;
    if ((s = getenv ("PMI_SPAWNED")))
        pmi->spawned = strtol (s, NULL, 10);
    *ops = &pmi_simple_operations;
    return pmi;
error:
    pmi_simple_client_destroy (pmi);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
