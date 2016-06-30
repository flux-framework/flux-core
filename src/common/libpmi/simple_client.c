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
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/param.h>

#include "simple_client.h"

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"

#define BUFSIZE 1024

struct pmi_simple_client {
    int fd;
    int rank;
    int size;
    int spawned;
    int initialized;
    int kvsname_max;
    int keylen_max;
    int vallen_max;
    char buf[BUFSIZE];
};

static int dgetline (int fd, char *buf, int len)
{
    int i = 0;
    while (i < len - 1) {
        if (read (fd, &buf[i], 1) <= 0)
            return -1;
        if (buf[i] == '\n')
            break;
        i++;
    }
    buf[i] = '\0';
    return 0;
}

int pmi_simple_client_init (struct pmi_simple_client *pmi, int *spawned)
{
    int result = PMI_FAIL;
    int rc, vers, subvers;

    if (dprintf (pmi->fd, "cmd=init pmi_version=1 pmi_subversion=1\n") < 0)
        goto done;
    if (dgetline (pmi->fd, pmi->buf, sizeof (pmi->buf)) < 0)
        goto done;
    if (sscanf (pmi->buf,
                "cmd=response_to_init pmi_version=%d pmi_subversion=%d rc=%d",
                &vers, &subvers, &rc) != 3)
        goto done;
    if (vers != 1 || subvers != 1 || rc != 0)
        goto done;
    if (dprintf (pmi->fd, "cmd=get_maxes\n") < 0)
        goto done;
    if (dgetline (pmi->fd, pmi->buf, sizeof (pmi->buf)) < 0)
        goto done;
    if (sscanf (pmi->buf,
                "cmd=maxes kvsname_max=%d keylen_max=%d vallen_max=%d",
                &pmi->kvsname_max, &pmi->keylen_max, &pmi->vallen_max) != 3)
        goto done;
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

    if (dprintf (pmi->fd, "cmd=finalize\n") < 0)
        goto done;
    if (dgetline (pmi->fd, pmi->buf, sizeof (pmi->buf)) < 0)
        goto done;
    if (strcmp (pmi->buf, "cmd=finalize_ack") != 0)
        goto done;
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
    int num;

    if (!pmi->initialized)
        goto done;
    if (dprintf (pmi->fd, "cmd=get_appnum\n") < 0)
        goto done;
    if (dgetline (pmi->fd, pmi->buf, sizeof (pmi->buf)) < 0)
        goto done;
    if (sscanf (pmi->buf, "cmd=appnum appnum=%d", &num) != 1)
        goto done;
    *appnum = num;
    result = PMI_SUCCESS;
done:
    return result;
}

int pmi_simple_client_get_universe_size (struct pmi_simple_client *pmi,
                                         int *universe_size)
{
    int result = PMI_FAIL;
    int size;

    if (!pmi->initialized)
        goto done;
    if (dprintf (pmi->fd, "cmd=get_universe_size\n") < 0)
        goto done;
    if (dgetline (pmi->fd, pmi->buf, sizeof (pmi->buf)) < 0)
        goto done;
    if (sscanf (pmi->buf, "cmd=universe_size size=%d", &size) != 1)
        goto done;
    *universe_size = size;
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

    if (!pmi->initialized)
        goto done;
    if (dprintf (pmi->fd, "cmd=barrier_in\n") < 0)
        goto done;
    if (dgetline (pmi->fd, pmi->buf, sizeof (pmi->buf)) < 0)
        goto done;
    if (strcmp (pmi->buf, "cmd=barrier_out") != 0)
        goto done;
    result = PMI_SUCCESS;
done:
    return result;
}

int pmi_simple_client_abort (struct pmi_simple_client *pmi,
                             int exit_code, const char *error_msg)
{
    return PMI_FAIL;
}

#define S_(x) #x
#define S(x) S_(x)
int pmi_simple_client_kvs_get_my_name (struct pmi_simple_client *pmi,
                                       char *kvsname, int length)
{
    char val[BUFSIZE + 1];
    int result = PMI_FAIL;

    if (!pmi->initialized)
        goto done;
    if (dprintf (pmi->fd, "cmd=get_my_kvsname\n") < 0)
        goto done;
    if (dgetline (pmi->fd, pmi->buf, sizeof (pmi->buf)) < 0)
        goto done;
    if (sscanf (pmi->buf, "cmd=my_kvsname kvsname=%" S(BUFSIZE) "s", val) != 1)
        goto done;
    if (strlen (val) >= length)
        goto done;
    strcpy (kvsname, val);
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

    if (!pmi->initialized)
        goto done;
    if (dprintf (pmi->fd, "cmd=put kvsname=%s key=%s value=%s\n",
                 kvsname, key, value) < 0)
        goto done;
    if (dgetline (pmi->fd, pmi->buf, sizeof (pmi->buf)) < 0)
        goto done;
    if (strcmp (pmi->buf, "cmd=put_result rc=0 msg=success") != 0)
        goto done;
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
    char val[BUFSIZE + 1];
    int result = PMI_FAIL;

    if (!pmi->initialized)
        goto done;
    if (dprintf (pmi->fd, "cmd=get kvsname=%s key=%s\n", kvsname, key) < 0)
        goto done;
    if (dgetline (pmi->fd, pmi->buf, sizeof (pmi->buf)) < 0)
        goto done;
    if (sscanf (pmi->buf, "cmd=get_result rc=0 msg=success value=%"
                                                    S(BUFSIZE) "s", val) != 1)
        goto done;
    if (strlen (val) >= len)
        goto done;
    strcpy (value, val);
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
        free (pmi);
    }
}

struct pmi_simple_client *pmi_simple_client_create (void)
{
    struct pmi_simple_client *pmi = xzmalloc (sizeof (*pmi));
    const char *s;

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
