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

#include "client.h"
#include "client_impl.h"

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"

#define BUFSIZE 1024

struct simple_impl {
    int fd;
    int rank;
    int size;
    int initialized;
    int kvsname_max;
    int keylen_max;
    int vallen_max;
    char buf[BUFSIZE];
};

void destroy_simple (void *arg)
{
    struct simple_impl *impl = arg;
    if (impl) {
        if (impl->fd != -1)
            (void)close (impl->fd);
        free (impl);
    }
}

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

static int simple_init (void *impl, int *spawned)
{
    struct simple_impl *s = impl;
    int result = PMI_FAIL;
    int rc, vers, subvers;

    if (dprintf (s->fd, "cmd=init pmi_version=1 pmi_subversion=1\n") < 0)
        goto done;
    if (dgetline (s->fd, s->buf, sizeof (s->buf)) < 0)
        goto done;
    if (sscanf (s->buf,
                "cmd=response_to_init pmi_version=%d pmi_subversion=%d rc=%d",
                &vers, &subvers, &rc) != 3)
        goto done;
    if (vers != 1 || subvers != 1 || rc != 0)
        goto done;
    if (dprintf (s->fd, "cmd=get_maxes\n") < 0)
        goto done;
    if (dgetline (s->fd, s->buf, sizeof (s->buf)) < 0)
        goto done;
    if (sscanf (s->buf, "cmd=maxes kvsname_max=%d keylen_max=%d vallen_max=%d",
                &s->kvsname_max, &s->keylen_max, &s->vallen_max) != 3)
        goto done;
    s->initialized = 1;
    if (spawned)
        *spawned = PMI_FALSE;
    result = PMI_SUCCESS;
done:
    return result;
}

static int simple_initialized (void *impl, int *initialized)
{
    struct simple_impl *s = impl;
    *initialized = s->initialized;
    return PMI_SUCCESS;
}

static int simple_finalize (void *impl)
{
    struct simple_impl *s = impl;
    int result = PMI_FAIL;

    if (dprintf (s->fd, "cmd=finalize\n") < 0)
        goto done;
    if (dgetline (s->fd, s->buf, sizeof (s->buf)) < 0)
        goto done;
    if (strcmp (s->buf, "cmd=finalize_ack") != 0)
        goto done;
    result = PMI_SUCCESS;
done:
    return result;
}

static int simple_get_size (void *impl, int *size)
{
    struct simple_impl *s = impl;
    if (!s->initialized)
        return PMI_FAIL;
    *size = s->size;
    return PMI_SUCCESS;
}

static int simple_get_rank (void *impl, int *rank)
{
    struct simple_impl *s = impl;
    if (!s->initialized)
        return PMI_FAIL;
    *rank = s->rank;
    return PMI_SUCCESS;
}

static int simple_get_appnum (void *impl, int *appnum)
{
    struct simple_impl *s = impl;
    int result = PMI_FAIL;
    int num;

    if (!s->initialized)
        goto done;
    if (dprintf (s->fd, "cmd=get_appnum\n") < 0)
        goto done;
    if (dgetline (s->fd, s->buf, sizeof (s->buf)) < 0)
        goto done;
    if (sscanf (s->buf, "cmd=appnum appnum=%d", &num) != 1)
        goto done;
    *appnum = num;
    result = PMI_SUCCESS;
done:
    return result;
}

static int simple_get_universe_size (void *impl, int *universe_size)
{
    struct simple_impl *s = impl;
    int result = PMI_FAIL;
    int size;

    if (!s->initialized)
        goto done;
    if (dprintf (s->fd, "cmd=get_universe_size\n") < 0)
        goto done;
    if (dgetline (s->fd, s->buf, sizeof (s->buf)) < 0)
        goto done;
    if (sscanf (s->buf, "cmd=universe_size size=%d", &size) != 1)
        goto done;
    *universe_size = size;
    result = PMI_SUCCESS;
done:
    return result;
}

static int simple_publish_name (void *impl,
        const char *service_name, const char *port)
{
    return PMI_FAIL;
}

static int simple_unpublish_name (void *impl,
        const char *service_name)
{
    return PMI_FAIL;
}

static int simple_lookup_name (void *impl,
        const char *service_name, char *port)
{
    return PMI_FAIL;
}

static int simple_barrier (void *impl)
{
    struct simple_impl *s = impl;
    int result = PMI_FAIL;

    if (!s->initialized)
        goto done;
    if (dprintf (s->fd, "cmd=barrier_in\n") < 0)
        goto done;
    if (dgetline (s->fd, s->buf, sizeof (s->buf)) < 0)
        goto done;
    if (strcmp (s->buf, "cmd=barrier_out") != 0)
        goto done;
    result = PMI_SUCCESS;
done:
    return result;
}

static int simple_abort (void *impl,
        int exit_code, const char *error_msg)
{
    return PMI_FAIL;
}

#define S_(x) #x
#define S(x) S_(x)
static int simple_kvs_get_my_name (void *impl,
        char *kvsname, int length)
{
    struct simple_impl *s = impl;
    char val[BUFSIZE + 1];
    int result = PMI_FAIL;

    if (!s->initialized)
        goto done;
    if (dprintf (s->fd, "cmd=get_my_kvsname\n") < 0)
        goto done;
    if (dgetline (s->fd, s->buf, sizeof (s->buf)) < 0)
        goto done;
    if (sscanf (s->buf, "cmd=my_kvsname kvsname=%" S(BUFSIZE) "s", val) != 1)
        goto done;
    if (strlen (val) >= length)
        goto done;
    strcpy (kvsname, val);
    result = PMI_SUCCESS;
done:
    return result;
}

static int simple_kvs_get_name_length_max (void *impl, int *length)
{
    struct simple_impl *s = impl;
    if (!s->initialized)
        return PMI_FAIL;
    *length = s->kvsname_max;
    return PMI_SUCCESS;
}

static int simple_kvs_get_key_length_max (void *impl, int *length)
{
    struct simple_impl *s = impl;
    if (!s->initialized)
        return PMI_FAIL;
    *length = s->keylen_max;
    return PMI_SUCCESS;
}

static int simple_kvs_get_value_length_max (void *impl, int *length)
{
    struct simple_impl *s = impl;
    if (!s->initialized)
        return PMI_FAIL;
    *length = s->vallen_max;
    return PMI_SUCCESS;
}

static int simple_kvs_put (void *impl,
        const char *kvsname, const char *key, const char *value)
{
    struct simple_impl *s = impl;
    int result = PMI_FAIL;

    if (!s->initialized)
        goto done;
    if (dprintf (s->fd, "cmd=put kvsname=%s key=%s value=%s\n",
                kvsname, key, value) < 0)
        goto done;
    if (dgetline (s->fd, s->buf, sizeof (s->buf)) < 0)
        goto done;
    if (strcmp (s->buf, "cmd=put_result rc=0 msg=success") != 0)
        goto done;
    result = PMI_SUCCESS;
done:
    return result;
}

static int simple_kvs_commit (void *impl, const char *kvsname)
{
    return PMI_SUCCESS; /* a no-op here */
}

static int simple_kvs_get (void *impl,
        const char *kvsname, const char *key, char *value, int len)
{
    struct simple_impl *s = impl;
    char val[BUFSIZE + 1];
    int result = PMI_FAIL;

    if (!s->initialized)
        goto done;
    if (dprintf (s->fd, "cmd=get kvsname=%s key=%s\n", kvsname, key) < 0)
        goto done;
    if (dgetline (s->fd, s->buf, sizeof (s->buf)) < 0)
        goto done;
    if (sscanf (s->buf, "cmd=get_result rc=0 msg=success value=%"
                                                    S(BUFSIZE) "s", val) != 1)
        goto done;
    if (strlen (val) >= len)
        goto done;
    strcpy (value, val);
    result = PMI_SUCCESS;
done:
    return result;
}

static int simple_spawn_multiple (void *impl,
        int count,
        const char *cmds[],
        const char **argvs[],
        const int maxprocs[],
        const int info_keyval_sizesp[],
        const pmi_keyval_t *info_keyval_vectors[],
        int preput_keyval_size,
        const pmi_keyval_t preput_keyval_vector[],
        int errors[])
{
    return PMI_FAIL;
}

pmi_t *pmi_create_simple (int fd, int rank, int size)
{
    struct simple_impl *s = xzmalloc (sizeof (*s));
    pmi_t *pmi;

    s->fd = fd;
    s->rank = rank;
    s->size = size;
    if (!(pmi = pmi_create (s, destroy_simple))) {
        destroy_simple (s);
        return NULL;
    }
    pmi->init = simple_init;
    pmi->initialized = simple_initialized;
    pmi->finalize = simple_finalize;
    pmi->get_size = simple_get_size;
    pmi->get_rank = simple_get_rank;
    pmi->get_appnum = simple_get_appnum;
    pmi->get_universe_size = simple_get_universe_size;
    pmi->publish_name = simple_publish_name;
    pmi->unpublish_name = simple_unpublish_name;
    pmi->lookup_name = simple_lookup_name;
    pmi->barrier = simple_barrier;
    pmi->abort = simple_abort;
    pmi->kvs_get_my_name = simple_kvs_get_my_name;
    pmi->kvs_get_name_length_max = simple_kvs_get_name_length_max;
    pmi->kvs_get_key_length_max = simple_kvs_get_key_length_max;
    pmi->kvs_get_value_length_max = simple_kvs_get_value_length_max;
    pmi->kvs_put = simple_kvs_put;
    pmi->kvs_commit = simple_kvs_commit;
    pmi->kvs_get = simple_kvs_get;
    pmi->spawn_multiple = simple_spawn_multiple;

    return pmi;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
