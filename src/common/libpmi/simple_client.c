/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

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

int pmi_simple_client_init (struct pmi_simple_client *pmi, int *spawned)
{
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

int pmi_simple_client_initialized (struct pmi_simple_client *pmi,
                                   int *initialized)
{
    *initialized = pmi->initialized;
    return PMI_SUCCESS;
}

int pmi_simple_client_finalize (struct pmi_simple_client *pmi)
{
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

int pmi_simple_client_get_size (struct pmi_simple_client *pmi, int *size)
{
    if (!pmi->initialized)
        return PMI_FAIL;
    *size = pmi->size;
    return PMI_SUCCESS;
}

int pmi_simple_client_get_rank (struct pmi_simple_client *pmi, int *rank)
{
    if (!pmi->initialized)
        return PMI_FAIL;
    *rank = pmi->rank;
    return PMI_SUCCESS;
}

int pmi_simple_client_get_appnum (struct pmi_simple_client *pmi, int *appnum)
{
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

int pmi_simple_client_get_universe_size (struct pmi_simple_client *pmi,
                                         int *universe_size)
{
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

int pmi_simple_client_publish_name (struct pmi_simple_client *pmi,
                                    const char *service_name, const char *port)
{
    return PMI_FAIL;
}

int pmi_simple_client_unpublish_name (struct pmi_simple_client *pmi,
                                      const char *service_name)
{
    return PMI_FAIL;
}

int pmi_simple_client_lookup_name (struct pmi_simple_client *pmi,
                                   const char *service_name, char *port)
{
    return PMI_FAIL;
}

int pmi_simple_client_barrier (struct pmi_simple_client *pmi)
{
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

int pmi_simple_client_abort (struct pmi_simple_client *pmi,
                             int exit_code, const char *error_msg)
{
    return PMI_FAIL;
}

int pmi_simple_client_kvs_get_my_name (struct pmi_simple_client *pmi,
                                       char *kvsname, int length)
{
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

int pmi_simple_client_kvs_get_name_length_max (struct pmi_simple_client *pmi,
                                               int *length)
{
    if (!pmi->initialized)
        return PMI_FAIL;
    *length = pmi->kvsname_max;
    return PMI_SUCCESS;
}

int pmi_simple_client_kvs_get_key_length_max (struct pmi_simple_client *pmi,
                                              int *length)
{
    if (!pmi->initialized)
        return PMI_FAIL;
    *length = pmi->keylen_max;
    return PMI_SUCCESS;
}

int pmi_simple_client_kvs_get_value_length_max (struct pmi_simple_client *pmi,
                                                int *length)
{
    if (!pmi->initialized)
        return PMI_FAIL;
    *length = pmi->vallen_max;
    return PMI_SUCCESS;
}

int pmi_simple_client_kvs_put (struct pmi_simple_client *pmi,
                               const char *kvsname, const char *key,
                               const char *value)
{
    int result = PMI_FAIL;
    int rc;

    if (!pmi->initialized)
        goto done;
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

int pmi_simple_client_kvs_commit (struct pmi_simple_client *pmi,
                                  const char *kvsname)
{
    return PMI_SUCCESS; /* a no-op here */
}

int pmi_simple_client_kvs_get (struct pmi_simple_client *pmi,
                               const char *kvsname,
                               const char *key, char *value, int len)
{
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

int pmi_simple_client_spawn_multiple (struct pmi_simple_client *pmi,
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

void pmi_simple_client_destroy (struct pmi_simple_client *pmi)
{
    if (pmi) {
        if (pmi->fd != -1)
            (void)close (pmi->fd);
        if (pmi->buf)
            free (pmi->buf);
        free (pmi);
    }
}

struct pmi_simple_client *pmi_simple_client_create (void)
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
    return pmi;
error:
    pmi_simple_client_destroy (pmi);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
