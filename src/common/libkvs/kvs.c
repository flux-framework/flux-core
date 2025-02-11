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

#include "src/common/libutil/blobref.h"

#include "treeobj.h"
#include "kvs_util_private.h"

flux_future_t *flux_kvs_namespace_create (flux_t *h,
                                          const char *ns,
                                          uint32_t owner,
                                          int flags)
{
    flux_future_t *rv = NULL;
    const char *hash_name;
    json_t *rootdir = NULL;
    char rootref[BLOBREF_MAX_STRING_SIZE];
    void *data = NULL;
    int len;

    if (!ns || flags) {
        errno = EINVAL;
        return NULL;
    }

    if (!(hash_name = flux_attr_get (h, "content.hash")))
        goto cleanup;

    if (!(rootdir = treeobj_create_dir ()))
        goto cleanup;

    if (!(data = treeobj_encode (rootdir)))
        goto cleanup;
    len = strlen (data);

    /* N.B. blobref of empty treeobj dir guaranteed to be in content store
     * b/c that is how the primary KVS is initialized.
     */
    if (blobref_hash (hash_name, data, len, rootref, sizeof (rootref)) < 0)
        goto cleanup;

    /* N.B. owner cast to int */
    rv = flux_rpc_pack (h,
                        "kvs.namespace-create",
                        0,
                        0,
                        "{ s:s s:s s:i s:i }",
                        "namespace", ns,
                        "rootref", rootref,
                        "owner", owner,
                        "flags", flags);
cleanup:
    json_decref (rootdir);
    free (data);
    return rv;
}

flux_future_t *flux_kvs_namespace_create_with (flux_t *h,
                                               const char *ns,
                                               const char *rootref,
                                               uint32_t owner,
                                               int flags)
{
    if (!ns || !rootref || flags) {
        errno = EINVAL;
        return NULL;
    }

    /* N.B. owner cast to int */
    return flux_rpc_pack (h,
                          "kvs.namespace-create",
                          0,
                          0,
                          "{ s:s s:s s:i s:i }",
                          "namespace", ns,
                          "rootref", rootref,
                          "owner", owner,
                          "flags", flags);
}

flux_future_t *flux_kvs_namespace_remove (flux_t *h, const char *ns)
{
    if (!ns) {
        errno = EINVAL;
        return NULL;
    }

    return flux_rpc_pack (h,
                          "kvs.namespace-remove",
                          0,
                          0,
                          "{ s:s }",
                          "namespace", ns);
}

int flux_kvs_get_version (flux_t *h, const char *ns, int *versionp)
{
    flux_future_t *f;
    int version;
    int rc = -1;

    if (!ns) {
        if (!(ns = kvs_get_namespace ()))
            return -1;
    }
    if (!(f = flux_rpc_pack (h,
                             "kvs.getroot",
                             FLUX_NODEID_ANY,
                             0,
                             "{ s:s }",
                             "namespace", ns)))
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

int flux_kvs_wait_version (flux_t *h, const char *ns, int version)
{
    flux_future_t *f;
    int ret = -1;

    if (!ns) {
        if (!(ns = kvs_get_namespace ()))
            return -1;
    }
    if (!(f = flux_rpc_pack (h,
                             "kvs.wait-version",
                             FLUX_NODEID_ANY,
                             0,
                             "{ s:i s:s }",
                             "rootseq", version,
                             "namespace", ns)))
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
