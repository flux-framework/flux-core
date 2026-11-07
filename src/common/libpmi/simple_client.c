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
#include <stdint.h>
#include <sys/param.h>
#include <assert.h>
#include <flux/taskmap.h>

#include "src/common/libutil/aux.h"

#include "simple_client.h"
#include "simple_server.h"
#include "keyval.h"
#include "pmi.h"

/* Thread safety: all wire I/O is under pmi->lock, which a request
 * holds for its round trip.
 */

/* Read one line from the fd into pmi->buf.  Buffering is done here
 * rather than in stdio so the fd can later be polled.
 * Returns 0 on success, -1 on error or EOF.  Call with lock held.
 */
static int read_line (struct pmi_simple_client *pmi)
{
    for (;;) {
        char *nl = memchr (pmi->rbuf, '\n', pmi->rlen);
        if (nl) {
            size_t linelen = nl - pmi->rbuf + 1;
            if (linelen >= pmi->buflen) {
                errno = EPROTO;
                return -1;
            }
            memcpy (pmi->buf, pmi->rbuf, linelen);
            pmi->buf[linelen] = '\0';
            memmove (pmi->rbuf, nl + 1, pmi->rlen - linelen);
            pmi->rlen -= linelen;
            return 0;
        }
        if (pmi->rlen == pmi->rbufsize) {
            errno = EPROTO; // line longer than any valid response
            return -1;
        }
        ssize_t n = read (pmi->fd, pmi->rbuf + pmi->rlen, pmi->rbufsize - pmi->rlen);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1; // EOF
        pmi->rlen += n;
    }
}

/* Read the response to a request, expecting 'cmd'.
 * Returns 0 on success, -1 on error.  Call with lock held.
 */
static int read_response (struct pmi_simple_client *pmi, const char *cmd)
{
    if (read_line (pmi) < 0)
        return -1;
    return keyval_parse_isword (pmi->buf, "cmd", cmd) == 0 ? 0 : -1;
}

/* Client sends exact required pmi_version and minimum required pmi_subversion.
 * Server sends client's pmi_version and maximum supported pmi_subversion.
 * See RFC 13 Version Handshake section for more detail.
 */
int pmi_simple_client_init (struct pmi_simple_client *pmi)
{
    int result = PMI_FAIL;
    unsigned int vers, subvers;
    int rc;

    if (!pmi)
        return PMI_ERR_INIT;
    pthread_mutex_lock (&pmi->lock);
    if (fprintf (pmi->f, "cmd=init pmi_version=1 pmi_subversion=1\n") < 0)
        goto done;
    if (read_line (pmi) < 0)
        goto done;
    if (keyval_parse_isword (pmi->buf, "cmd", "response_to_init") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    if (keyval_parse_uint (pmi->buf, "pmi_version", &vers) < 0
        || keyval_parse_uint (pmi->buf, "pmi_subversion", &subvers) < 0)
        goto done;
    if (vers != 1 || subvers < 1)
        goto done;

    if (fprintf (pmi->f, "cmd=get_maxes\n") < 0)
        goto done;
    if (read_line (pmi) < 0)
        goto done;
    if (keyval_parse_isword (pmi->buf, "cmd", "maxes") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    if (keyval_parse_uint (pmi->buf, "kvsname_max", &pmi->kvsname_max) < 0
        || keyval_parse_uint (pmi->buf, "keylen_max", &pmi->keylen_max) < 0
        || keyval_parse_uint (pmi->buf, "vallen_max", &pmi->vallen_max) < 0)
        goto done;
    /* Size the line buffers for the largest possible response. */
    int buflen = pmi->keylen_max + pmi->vallen_max + pmi->kvsname_max
                 + SIMPLE_MAX_PROTO_OVERHEAD;
    if (buflen > pmi->buflen) {
        char *buf;
        char *rbuf;
        if (!(buf = calloc (1, buflen)) || !(rbuf = calloc (1, buflen))) {
            free (buf);
            result = PMI_ERR_NOMEM;
            goto done;
        }
        memcpy (rbuf, pmi->rbuf, pmi->rlen);
        free (pmi->buf);
        free (pmi->rbuf);
        pmi->buf = buf;
        pmi->rbuf = rbuf;
        pmi->buflen = pmi->rbufsize = buflen;
    }
    pmi->initialized = 1;
    result = PMI_SUCCESS;
done:
    pthread_mutex_unlock (&pmi->lock);
    return result;
}

int pmi_simple_client_finalize (struct pmi_simple_client *pmi)
{
    int result = PMI_FAIL;
    int rc;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    pthread_mutex_lock (&pmi->lock);
    if (fprintf (pmi->f, "cmd=finalize\n") < 0)
        goto done;
    if (read_response (pmi, "finalize_ack") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    result = PMI_SUCCESS;
done:
    pthread_mutex_unlock (&pmi->lock);
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
    pthread_mutex_lock (&pmi->lock);
    if (fprintf (pmi->f, "cmd=get_appnum\n") < 0)
        goto done;
    if (read_response (pmi, "appnum") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    if (keyval_parse_int (pmi->buf, "appnum", appnum) < 0)
        goto done;
    result = PMI_SUCCESS;
done:
    pthread_mutex_unlock (&pmi->lock);
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
    pthread_mutex_lock (&pmi->lock);
    if (fprintf (pmi->f, "cmd=get_universe_size\n") < 0)
        goto done;
    if (read_response (pmi, "universe_size") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    if (keyval_parse_int (pmi->buf, "size", universe_size) < 0)
        goto done;
    result = PMI_SUCCESS;
done:
    pthread_mutex_unlock (&pmi->lock);
    return result;
}

int pmi_simple_client_barrier (struct pmi_simple_client *pmi)
{
    int result = PMI_FAIL;
    int rc;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    pthread_mutex_lock (&pmi->lock);
    if (fprintf (pmi->f, "cmd=barrier_in\n") < 0)
        goto done;
    if (read_response (pmi, "barrier_out") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    result = PMI_SUCCESS;
done:
    pthread_mutex_unlock (&pmi->lock);
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
    pthread_mutex_lock (&pmi->lock);
    if (fprintf (pmi->f, "cmd=get_my_kvsname\n") < 0)
        goto done;
    if (read_response (pmi, "my_kvsname") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    if (keyval_parse_word (pmi->buf, "kvsname", kvsname, length) < 0)
        goto done;
    result = PMI_SUCCESS;
done:
    pthread_mutex_unlock (&pmi->lock);
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
    pthread_mutex_lock (&pmi->lock);
    if (fprintf (pmi->f, "cmd=put kvsname=%s key=%s value=%s\n",
                 kvsname, key, value) < 0)
        goto done;
    if (read_response (pmi, "put_result") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    result = PMI_SUCCESS;
done:
    pthread_mutex_unlock (&pmi->lock);
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
    pthread_mutex_lock (&pmi->lock);
    if (fprintf (pmi->f, "cmd=get kvsname=%s key=%s\n", kvsname, key) < 0)
        goto done;
    if (read_response (pmi, "get_result") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    if (keyval_parse_string (pmi->buf, "value", value, len) < 0)
        goto done;
    result = PMI_SUCCESS;
done:
    pthread_mutex_unlock (&pmi->lock);
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
    pthread_mutex_lock (&pmi->lock);
    if (fprintf (pmi->f,
                 "cmd=abort exitcode=%d%s%s\n",
                 exit_code,
                 msg ? " error_msg=" : "",
                 msg ? msg : "") < 0) {
        pthread_mutex_unlock (&pmi->lock);
        goto done;
    }
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
        (void)pthread_mutex_destroy (&pmi->lock);
        free (pmi->rbuf);
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
    if (pthread_mutex_init (&pmi->lock, NULL) != 0) {
        free (pmi);
        return NULL;
    }
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
    if (!(pmi->f = fdopen (fd, "w"))) // pmi->f takes ownership of fd
        goto error;
    setvbuf (pmi->f, NULL, _IONBF, 0); // each fprintf is one write()
    pmi->fd = fd;
    fd = -1;
    pmi->buflen = pmi->rbufsize = SIMPLE_MAX_PROTO_LINE;
    if (!(pmi->buf = calloc (1, pmi->buflen))
        || !(pmi->rbuf = calloc (1, pmi->rbufsize)))
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
