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
#include <assert.h>
#include <flux/taskmap.h>

#include "src/common/libutil/aux.h"

#include "simple_client.h"
#include "simple_server.h"
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
    if (fprintf (pmi->f, "cmd=init pmi_version=1 pmi_subversion=1\n") < 0)
        goto done;
    if (!fgets (buf, sizeof (buf), pmi->f))
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

    if (fprintf (pmi->f, "cmd=get_maxes\n") < 0)
        goto done;
    if (!fgets (buf, sizeof (buf), pmi->f))
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
    if (fprintf (pmi->f, "cmd=finalize\n") < 0)
        goto done;
    if (!fgets (pmi->buf, pmi->buflen, pmi->f))
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
    if (!appnum)
        return PMI_ERR_INVALID_ARG;
    if (fprintf (pmi->f, "cmd=get_appnum\n") < 0)
        goto done;
    if (!fgets (pmi->buf, pmi->buflen, pmi->f))
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
    if (!universe_size)
        return PMI_ERR_INVALID_ARG;
    if (fprintf (pmi->f, "cmd=get_universe_size\n") < 0)
        goto done;
    if (!fgets (pmi->buf, pmi->buflen, pmi->f))
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
    if (fprintf (pmi->f, "cmd=barrier_in\n") < 0)
        goto done;
    if (!fgets (pmi->buf, pmi->buflen, pmi->f))
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
    if (!kvsname || length <= 0)
        return PMI_ERR_INVALID_ARG;
    if (fprintf (pmi->f, "cmd=get_my_kvsname\n") < 0)
        goto done;
    if (!fgets (pmi->buf, pmi->buflen, pmi->f))
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
    if (!kvsname || !key || !value)
        return PMI_ERR_INVALID_ARG;
    if (fprintf (pmi->f, "cmd=put kvsname=%s key=%s value=%s\n",
                 kvsname, key, value) < 0)
        goto done;
    if (!fgets (pmi->buf, pmi->buflen, pmi->f))
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
    if (!kvsname || !key || !value || len <= 0)
        return PMI_ERR_INVALID_ARG;
    if (fprintf (pmi->f, "cmd=get kvsname=%s key=%s\n", kvsname, key) < 0)
        goto done;
    if (!fgets (pmi->buf, pmi->buflen, pmi->f))
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

/* Helper for get_clique_size(), get_clique_ranks().
 * Fetch 'PMI_process_mapping' from the KVS and parse.
 * On success, a struct taskmap is stored in the pmi client aux hash
 *  and returned to the caller.
 */
static struct taskmap *fetch_taskmap (struct pmi_simple_client *pmi)
{
    struct taskmap *map = NULL;
    int result;
    char *nom;
    char *val;

    assert (pmi != NULL);
    assert (pmi->initialized);

    if ((map = pmi_simple_client_aux_get (pmi, "taskmap")))
        return map;

    nom = calloc (1, pmi->kvsname_max);
    val = calloc (1, pmi->vallen_max);
    if (!nom || !val) {
        result = PMI_ERR_NOMEM;
        goto done;
    }
    result = pmi_simple_client_kvs_get_my_name (pmi, nom, pmi->kvsname_max);
    if (result != PMI_SUCCESS)
        goto done;
    /*  First try flux.taskmap, falling back to PMI_process_mapping if it
     *  does not exist (e.g. if process manager is not Flux).
     */
    result = pmi_simple_client_kvs_get (pmi,
                                        nom,
                                        "flux.taskmap",
                                        val,
                                        pmi->vallen_max);
    if (result != PMI_SUCCESS) {
        result = pmi_simple_client_kvs_get (pmi,
                                            nom,
                                            "PMI_process_mapping",
                                            val,
                                            pmi->vallen_max);
        if (result != PMI_SUCCESS)
            goto done;
    }
    if (!(map = taskmap_decode (val, NULL))) {
        result = PMI_FAIL;
        goto done;
    }
    if (pmi_simple_client_aux_set (pmi,
                                   "taskmap",
                                   map,
                                   (flux_free_f) taskmap_destroy) < 0) {
        taskmap_destroy (map);
        map = NULL;
    }
done:
    free (nom);
    free (val);
    return map;
}

int pmi_simple_client_get_clique_size (struct pmi_simple_client *pmi,
                                       int *size)
{
    int nodeid = -1;
    struct taskmap *map;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    if (!size)
        return PMI_ERR_INVALID_ARG;
    if (!(map = fetch_taskmap (pmi)) || taskmap_unknown (map)) {
        *size = 1;
        return PMI_SUCCESS;
    }
    if ((nodeid = taskmap_nodeid (map, pmi->rank)) < 0
        || (*size = taskmap_ntasks (map, nodeid)) < 0)
        return PMI_FAIL;
    return PMI_SUCCESS;
}

int pmi_simple_client_get_clique_ranks (struct pmi_simple_client *pmi,
                                        int ranks[],
                                        int length)
{
    struct taskmap *map;
    const struct idset *ids;
    int ntasks;
    int nodeid;
    unsigned int i;
    int index = 0;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    if (!ranks)
        return PMI_ERR_INVALID_ARG;
    map = fetch_taskmap (pmi);
    if (!map || taskmap_unknown (map)) {
        if (length != 1)
            return PMI_ERR_INVALID_SIZE;
        *ranks = pmi->rank;
        return PMI_SUCCESS;
    }
    if ((nodeid = taskmap_nodeid (map, pmi->rank)) < 0
        || (ntasks = taskmap_ntasks (map, nodeid)) < 0)
        return PMI_FAIL;
    if (ntasks > length)
        return PMI_ERR_INVALID_SIZE;
    if (!(ids = taskmap_taskids (map, nodeid)))
        return PMI_FAIL;
    i = idset_first (ids);
    while (i != IDSET_INVALID_ID) {
        ranks[index++] = i;
        i = idset_next (ids, i);
    }
    return PMI_SUCCESS;
}

int pmi_simple_client_abort (struct pmi_simple_client *pmi,
                             int exit_code,
                             const char *msg)
{
    int result = PMI_FAIL;
    char *cpy = NULL;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    if (exit_code < 0)
        return PMI_ERR_INVALID_ARG;
    /* If message includes embedded \n's that would interfere with
     * the wire protocol, replace them with spaces.
     */
    if (strchr (msg, '\n')) {
        if (!(cpy = strdup (msg)))
            return PMI_ERR_NOMEM;
        for (char *cp = cpy; *cp != '\0'; cp++) {
            if (*cp == '\n')
                *cp = ' ';
        }
        msg = cpy;
    }
    if (fprintf (pmi->f,
                 "cmd=abort exitcode=%d%s%s\n",
                 exit_code,
                 msg ? " error_msg=" : "",
                 msg ? msg : "") < 0)
        goto done;
    exit (exit_code);
    /* NOTREACHED */
done:
    free (cpy);
    return result;
}

void *pmi_simple_client_aux_get (struct pmi_simple_client *pmi,
                                 const char *name)
{
    if (!pmi) {
        errno = EINVAL;
        return NULL;
    }
    return aux_get (pmi->aux, name);
}

int pmi_simple_client_aux_set (struct pmi_simple_client *pmi,
                               const char *name,
                               void *aux,
                               flux_free_f destroy)
{
    if (!pmi) {
        errno = EINVAL;
        return -1;
    }
    return aux_set (&pmi->aux, name, aux, destroy);
}

void pmi_simple_client_destroy (struct pmi_simple_client *pmi)
{
    if (pmi) {
        int saved_errno = errno;
        aux_destroy (&pmi->aux);
        if (pmi->f)
            (void)fclose (pmi->f);
        free (pmi->buf);
        free (pmi);
        errno = saved_errno;
    }
}

struct pmi_simple_client *pmi_simple_client_create_fd (const char *pmi_fd,
                                                       const char *pmi_rank,
                                                       const char *pmi_size,
                                                       const char *pmi_spawned)
{
    struct pmi_simple_client *pmi;
    int fd = -1;

    if (!pmi_fd || !pmi_rank || !pmi_size) {
        errno = EINVAL;
        return NULL;
    }
    if (!(pmi = calloc (1, sizeof (*pmi))))
        return NULL;
    errno = 0;
    fd = strtol (pmi_fd, NULL, 10);
    pmi->rank = strtol (pmi_rank, NULL, 10);
    pmi->size = strtol (pmi_size, NULL, 10);
    if (errno != 0 || fd < 0 || pmi->rank < 0 || pmi->size < 1)
        goto error;
    if (pmi_spawned) {
        errno = 0;
        pmi->spawned = strtol (pmi_spawned, NULL, 10);
        if (errno != 0)
            goto error;
    }
    if (!(pmi->f = fdopen (fd, "r+"))) // pmi->f takes ownership of fd
        goto error;
    return pmi;
error:
    if (fd >= 0)
        (void)close (fd);
    pmi_simple_client_destroy (pmi);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
