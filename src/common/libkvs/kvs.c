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
#include <unistd.h>

#include <jansson.h>
#include <flux/core.h>

#define FLUX_HANDLE_KVS_NAMESPACE "kvsnamespace"

struct flux_kvs_namespace_itr {
    json_t *namespace_array;
    int size;
    int index;
};

flux_future_t *flux_kvs_namespace_create (flux_t *h, const char *namespace,
                                          uint32_t owner, int flags)
{
    if (!namespace || flags) {
        errno = EINVAL;
        return NULL;
    }

    /* N.B. owner cast to int */
    return flux_rpc_pack (h, "kvs.namespace-create", 0, 0,
                          "{ s:s s:i s:i }",
                          "namespace", namespace,
                          "owner", owner,
                          "flags", flags);
}

flux_future_t *flux_kvs_namespace_remove (flux_t *h, const char *namespace)
{
    if (!namespace) {
        errno = EINVAL;
        return NULL;
    }

    return flux_rpc_pack (h, "kvs.namespace-remove", 0, 0,
                          "{ s:s }",
                          "namespace", namespace);
}

flux_kvs_namespace_itr_t *flux_kvs_namespace_list (flux_t *h)
{
    flux_future_t *f;
    json_t *array = NULL;
    flux_kvs_namespace_itr_t *itr = NULL;

    if (!h) {
        errno = EINVAL;
        goto error;
    }
    if (!(f = flux_rpc (h, "kvs.namespace-list", NULL, FLUX_NODEID_ANY, 0)))
        goto error;
    if (flux_rpc_get_unpack (f, "{ s:O }", "namespaces", &array) < 0)
        goto error;
    if (!json_is_array (array)) {
        errno = EPROTO;
        goto error;
    }
    if (!(itr = calloc (1, sizeof (*itr)))) {
        errno = ENOMEM;
        goto error;
    }
    itr->namespace_array = array;
    itr->size = json_array_size (array);
    itr->index = 0;
    return itr;

error:
    flux_kvs_namespace_itr_destroy (itr);
    json_decref (array);
    return NULL;
}

const char *flux_kvs_namespace_itr_next (flux_kvs_namespace_itr_t *itr,
                                         uint32_t *owner,
                                         int *flags)
{
    const char *namespace;
    uint32_t owner_tmp;
    int flags_tmp;
    json_t *o;

    if (!itr) {
        errno = EINVAL;
        return NULL;
    }

    if (itr->size == itr->index) {
        errno = 0;
        return NULL;
    }

    if (!(o = json_array_get (itr->namespace_array, itr->index))) {
        errno = EPROTO;
        return NULL;
    }

    if (json_unpack (o, "{ s:s s:i s:i }",
                     "namespace", &namespace,
                     "owner", &owner_tmp,
                     "flags", &flags_tmp) < 0)
        return NULL;
    if (owner)
        (*owner) = owner_tmp;
    if (flags)
        (*flags) = flags_tmp;
    itr->index++;
    return namespace;
}

void flux_kvs_namespace_itr_rewind (flux_kvs_namespace_itr_t *itr)
{
    if (itr)
        itr->index = 0;
}

void flux_kvs_namespace_itr_destroy (flux_kvs_namespace_itr_t *itr)
{
    if (itr) {
        json_decref (itr->namespace_array);
        free (itr);
    }
}

int flux_kvs_set_namespace (flux_t *h, const char *namespace)
{
    char *str;

    if (!h || !namespace) {
        errno = EINVAL;
        return -1;
    }

    if (!(str = strdup (namespace))) {
        errno = ENOMEM;
        return -1;
    }

    if (flux_aux_set (h, FLUX_HANDLE_KVS_NAMESPACE, str, free) < 0) {
        int saved_errno = errno;
        free (str);
        errno = saved_errno;
        return -1;
    }
    return 0;
}

const char *flux_kvs_get_namespace (flux_t *h)
{
    const char *namespace;

    if (!h) {
        errno = EINVAL;
        return NULL;
    }

    if ((namespace = flux_aux_get (h, FLUX_HANDLE_KVS_NAMESPACE)))
        return namespace;

    if ((namespace = getenv ("FLUX_KVS_NAMESPACE")))
        return namespace;

    return KVS_PRIMARY_NAMESPACE;
}

int flux_kvs_get_version (flux_t *h, int *versionp)
{
    flux_future_t *f;
    const char *namespace;
    int version;
    int rc = -1;

    if (!(namespace = flux_kvs_get_namespace (h)))
        return -1;
    if (!(f = flux_rpc_pack (h, "kvs.getroot", FLUX_NODEID_ANY, 0, "{ s:s }",
                             "namespace", namespace)))
        goto done;
    if (flux_rpc_get_unpack (f, "{ s:i }", "rootseq", &version) < 0)
        goto done;
    if (versionp)
        *versionp = version;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

int flux_kvs_wait_version (flux_t *h, int version)
{
    flux_future_t *f;
    const char *namespace;
    int ret = -1;

    if (!(namespace = flux_kvs_get_namespace (h)))
        return -1;
    if (!(f = flux_rpc_pack (h, "kvs.sync", FLUX_NODEID_ANY, 0, "{ s:i s:s }",
                             "rootseq", version,
                             "namespace", namespace)))
        goto done;
    /* N.B. response contains (rootseq, rootref) but we don't need it.
     */
    if (flux_future_get (f, NULL) < 0)
        goto done;
    ret = 0;
done:
    flux_future_destroy (f);
    return ret;
}

int flux_kvs_dropcache (flux_t *h)
{
    flux_future_t *f;
    int rc = -1;

    if (!(f = flux_rpc (h, "kvs.dropcache", NULL, FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_future_get (f, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
