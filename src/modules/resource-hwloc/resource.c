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

#include <flux/core.h>

#include <stdarg.h>
#include <hwloc.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <inttypes.h>
#include <jansson.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libidset/idset.h"

#include "aggregate.h"


typedef struct
{
    uint32_t rank;
    unsigned int reload_count;  // Sequence number for reload request
    hwloc_topology_t topology;
} resource_ctx_t;

static int ctx_hwloc_init (flux_t *h, resource_ctx_t *ctx)
{
    int ret = -1;
    char *key = NULL;
    hwloc_bitmap_t restrictset = NULL;
    uint32_t hwloc_version;
    flux_future_t *f = NULL;
    const char *path = NULL;

    hwloc_version = hwloc_get_api_version ();
    if ((hwloc_version >> 16) != (HWLOC_API_VERSION >> 16)) {
        flux_log (h, LOG_ERR, "%s: compiled for hwloc API 0x%x but running on "
                        "library API 0x%x", __FUNCTION__, HWLOC_API_VERSION,
                        hwloc_version);
        goto done;
    }
    if (ctx->topology) {
        hwloc_topology_destroy (ctx->topology);
        ctx->topology = NULL;
    }
    if (hwloc_topology_init (&ctx->topology) < 0) {
        flux_log_error (h, "flux_topology_init");
        goto done;
    }
    if (hwloc_topology_set_flags (ctx->topology,
                                  HWLOC_TOPOLOGY_FLAG_IO_DEVICES) < 0)
        flux_log (h, LOG_ERR, "hwloc_topology_set_flags FLAG_IO_DEVICES failed");
    if (hwloc_topology_ignore_type (ctx->topology, HWLOC_OBJ_CACHE) < 0)
        flux_log (h, LOG_ERR, "hwloc_topology_ignore_type OBJ_CACHE failed");
    if (hwloc_topology_ignore_type (ctx->topology, HWLOC_OBJ_GROUP) < 0)
        flux_log (h, LOG_ERR, "hwloc_topology_ignore_type OBJ_GROUP failed");
    key = xasprintf ("config.resource.hwloc.xml.%" PRIu32, ctx->rank);
    if (!(f = flux_kvs_lookup (h, 0, key))) {
        flux_log_error (h, "flux_kvs_lookup");
        goto done;
    }
    if (flux_kvs_lookup_get_unpack (f, "s", &path) < 0) {
        flux_future_destroy (f);
        if (!(f = flux_kvs_lookup (h, 0, "config.resource.hwloc.default_xml"))) {
            flux_log_error (h, "flux_kvs_lookup");
            goto done;
        }
        if (flux_kvs_lookup_get_unpack (f, "s", &path) < 0)
            path = NULL;
    }

    if (path) {
        flux_log (h, LOG_INFO, "loading hwloc from %s", path);
        if (hwloc_topology_set_xml (ctx->topology, path) < 0) {
            flux_log_error (h, "hwloc_topology_set_xml");
            goto done;
        }
    }
    if (hwloc_topology_load (ctx->topology) < 0) {
        flux_log_error (h, "hwloc_topology_load");
        if (path)
            errno = ENOENT;
        goto done;
    }

    if (!path) {  // Only restrict the topology if using the host topology
        // Mask off hardware that we can't use
        if (!(restrictset = hwloc_bitmap_alloc ())) {
            flux_log_error (h, "hwloc_bitmap_alloc");
            goto done;
        }
        if (hwloc_get_cpubind (ctx->topology, restrictset,
                               HWLOC_CPUBIND_PROCESS) < 0) {
            flux_log_error (h, "hwloc_get_cpubind");
            goto done;
        }
        if (hwloc_topology_restrict (ctx->topology, restrictset, 0) < 0) {
            flux_log_error (h, "hwloc_topology_restrict");
            goto done;
        }
    }
    ret = 0;
done:
    if (restrictset)
        hwloc_bitmap_free (restrictset);
    flux_future_destroy (f);
    free (key);
    return ret;
}

static void resource_hwloc_ctx_destroy (resource_ctx_t *ctx)
{
    if (ctx) {
        if (ctx->topology)
            hwloc_topology_destroy (ctx->topology);
        free (ctx);
    }
}

static resource_ctx_t *resource_hwloc_ctx_create (flux_t *h)
{
    resource_ctx_t *ctx = xzmalloc (sizeof(resource_ctx_t));
    if (flux_get_rank (h, &ctx->rank) < 0) {
        flux_log_error (h, "flux_get_rank");
        goto error;
    }
    if (ctx_hwloc_init (h, ctx)) {
        flux_log_error (h, "hwloc context could not be created");
        goto error;
    }
    return ctx;
error:
    resource_hwloc_ctx_destroy (ctx);
    return NULL;
}

static int load_xml_to_kvs (flux_t *h, resource_ctx_t *ctx,
                            flux_kvs_txn_t *txn)
{
    char *xml_path = NULL;
    char *buffer = NULL;
    int buflen = 0, ret = -1;

    xml_path = xasprintf ("resource.hwloc.xml.%" PRIu32, ctx->rank);
    if (flux_kvs_txn_unlink (txn, 0, xml_path) < 0) {
        flux_log_error (h, "%s: flux_kvs_txn_unlink", __FUNCTION__);
        goto done;
    }
    if (hwloc_topology_export_xmlbuffer (ctx->topology, &buffer, &buflen) < 0) {
        flux_log_error (h, "%s: hwloc_topology_export_xmlbuffer", __FUNCTION__);
        goto done;
    }
    if (flux_kvs_txn_pack (txn, 0, xml_path, "s", buffer) < 0) {
        flux_log_error (h, "%s: flux_kvs_txn_pack", __FUNCTION__);
        goto done;
    }
    ret = 0;
done:
    if (xml_path)
        free (xml_path);
    hwloc_free_xmlbuffer (ctx->topology, buffer);
    return ret;
}

static int hwloc_gpu_count (hwloc_topology_t topology)
{
    int nobjs = 0;
    hwloc_obj_t obj = NULL;
    while ((obj = hwloc_get_next_osdev (topology, obj))) {
        /* Only count cudaX and openclX devices for now */
        const char *s = hwloc_obj_get_info_by_name (obj, "Backend");
        if (s && ((strcmp (s, "CUDA") == 0) || (strcmp (s, "OpenCL") == 0)))
            nobjs++;
    }
    return (nobjs);
}

static json_t *topo_info_tojson (hwloc_topology_t topology)
{
    int nobj, i;
    json_t *o = NULL;
    json_t *v = NULL;
    int depth = hwloc_topology_get_depth (topology);

    if (!(o = json_object ()))
        return NULL;
    for (i = 0; i < depth; i++) {
        hwloc_obj_type_t t = hwloc_get_depth_type (topology, i);
        nobj = hwloc_get_nbobjs_by_depth (topology, i);
        /* Skip "Machine" = 1 */
        if (t == HWLOC_OBJ_MACHINE && nobj == 1)
            continue;
        if (!(v = json_integer (nobj)))
            goto error;
        if (json_object_set_new (o, hwloc_obj_type_string (t), v) < 0)
            goto error;
    }
    if ((nobj = hwloc_gpu_count (topology))) {
        if (!(v = json_integer (nobj))
            || json_object_set_new (o, "GPU", v) < 0)
            goto error;
    }
    return (o);
error:
    json_decref (o);
    return (NULL);
}

static int aggregate_key_get (char *dst, size_t len, unsigned int seq)
{
    int n;
    if ((n = snprintf (dst, len, "resource.hwloc.reload:%u", seq) < 0)
        || (n >= len))
        return (-1);
    return 0;
}

/*   Wait for current reload to complete by waiting for current generation
 *    of aggregate topology information in kvs.
 */
static int reload_event_wait (flux_t *h, resource_ctx_t *ctx)
{
    int rc = -1;
    flux_future_t *f = NULL;
    char key [1024];
    flux_log (h, LOG_DEBUG, "seq=%d: waiting for reload", ctx->reload_count);
    if (aggregate_key_get (key, sizeof (key), ctx->reload_count) < 0
        || !(f = aggregate_wait (h, key))) {
        flux_log_error (h, "reload_event_wait: aggregate_wait");
        goto done;
    }
    if (flux_future_wait_for (f, 5.) < 0) {
        flux_log_error (h, "reload_event_wait: flux_future_wait_for");
        goto done;
    }
    rc = aggregate_unpack (f, "resource.hwloc.by_rank");
    flux_log (h, LOG_DEBUG, "seq=%d: reload complete", ctx->reload_count);
done:
    flux_future_destroy (f);
    return (rc);
}

static int aggregate_push_rank_info (flux_t *h, resource_ctx_t *ctx,
                                     unsigned int seq)
{
    int rc = -1;
    char key [1024];
    flux_future_t *f = NULL;
    json_t *o = NULL;

    if (aggregate_key_get (key, sizeof (key), seq) < 0) {
        flux_log_error (h, "%s: aggregate_key_get", __FUNCTION__);
    }
    if (!(o = topo_info_tojson (ctx->topology))) {
        flux_log_error (h, "%s: topo_info_tojson", __FUNCTION__);
        goto done;
    }
    if (!(f = aggregator_push_json (h, key, o))
        || (flux_future_get (f, NULL) < 0)) {
        flux_log_error (h, "%s: aggregator.push", __FUNCTION__);
        goto done;
    }
    rc = 0;
done:
    flux_future_destroy (f);
    return (rc);
}

static int load_hwloc (flux_t *h, resource_ctx_t *ctx)
{
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;
    int rc = -1;

    if (!(txn = flux_kvs_txn_create ())) {
        flux_log_error (h, "%s: flux_kvs_txn_create", __FUNCTION__);
        goto done;
    }
    if (load_xml_to_kvs (h, ctx, txn) < 0) {
        flux_log_error (h, "%s: failed to load xml to kvs", __FUNCTION__);
        goto done;
    }
    if (ctx->rank == 0
        && (flux_kvs_txn_unlink (txn, 0, "resource.hwloc.by_rank") < 0)) {
        flux_log_error (h, "%s: failed to unlink %s", __FUNCTION__,
                            "resource.hwloc.by_rank");
        goto done;
    }
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0) {
        flux_log_error (h, "%s: flux_kvs_commit", __FUNCTION__);
        goto done;
    }
    rc = 0;
done:
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    return rc;
}

/*  Reload hwloc on ranks in nodeset `ranks`, then all ranks re-aggregate
 *   the topology rank info (allowing synchronization on the result).
 *
 *  The sequence number `seq` is used to form a unique aggregation key in
 *   the kvs for each reload request.
 */
static int reload_hwloc (flux_t *h, resource_ctx_t *ctx,
                         const char *ranks, unsigned int seq)
{
    int rc = -1;
    bool all = false;
    struct idset *ids = NULL;

    if (strcmp (ranks, "all") == 0)
        all = true;
    if (!all && !(ids = idset_decode (ranks))) {
        flux_log_error (h, "reload_event_cb: idset_decode (%s)", ranks);
        goto out;
    }
    /*  Only perform reload if this rank was targeted inthe ranks field
     *   of the current event
     */
    if (all || idset_test (ids, ctx->rank)) {
        /* Re-initialize ctx and reload hwloc on this rank */
        if (ctx_hwloc_init (h, ctx) < 0) {
            flux_log_error (h, "ctx_hwloc_init");
            goto out;
        }
        if (load_hwloc (h, ctx) < 0) {
            flux_log_error (h, "load_hwloc");
            goto out;
        }
    }
    /*  All ranks push an aggregate back to kvs. Errors are logged in
     *   aggregate_push_rank_info()
     */
    rc = aggregate_push_rank_info (h, ctx, seq);
out:
    idset_destroy (ids);
    return (rc);
}

static bool valid_ranks (flux_t *h, const char *ranks)
{
    uint32_t size;
    struct idset *ids = NULL;
    bool rv = false;

    if (strcmp (ranks, "all") == 0)
        return true;
    if (!(ids = idset_decode (ranks))
        || (flux_get_size (h, &size) < 0)
        || (idset_last (ids) > size -1))
        goto out;
    rv = true;
out:
    idset_destroy (ids);
    return (rv);
}

/* Handle a reload RPC. This handler is only active on rank 0 */
static void reload_request_cb (flux_t *h,
                               flux_msg_handler_t *mh,
                               const flux_msg_t *msg,
                               void *arg)
{
    resource_ctx_t *ctx = arg;
    flux_future_t *f = NULL;
    const char *ranks;
    int errnum = ENOSYS;

    if (ctx->rank != 0)
        goto out;
    if (flux_request_unpack (msg, NULL, "{s:s}", "ranks", &ranks) < 0) {
        errnum = errno;
        goto out;
    }
    if (!valid_ranks (h, ranks)) {
        errnum = EHOSTUNREACH;
        goto out;
    }
    /*  Issue reload/aggregate.push on rank 0 before sending event
     *   to other ranks. This is becuase rank 0 wants to synchronously
     *   wait for aggregate completion, and therefore it will not be
     *   able to process the global event.
     */
    ctx->reload_count++;
    if (reload_hwloc (h, ctx, ranks, ctx->reload_count) < 0) {
        errnum = errno;
        flux_log_error (h, "load_hwloc_and_aggregate");
        goto out;
    }
    flux_log (h, LOG_DEBUG, "reload request: ranks=%s seq=%u",
                            ranks, ctx->reload_count);
    /*  Send a reload event to all ranks, specifying that targeted "ranks"
     *   only should reload hwloc. (this will be ignored on rank 0)
     */
    if (!(f = flux_event_publish_pack (h, "resource-hwloc.reload", 0,
                                       "{s:s,s:i}",
                                       "ranks", ranks,
                                       "sequence", ctx->reload_count))
        || (flux_future_get (f, 0) < 0)) {
        errnum = errno;
    }
    /*  Now wait for completion of the aggregate before responding to
     *   reload RPC.
     */
    if (reload_event_wait (h, ctx) < 0) {
        flux_log_error (h, "reload_request: wait_for_aggregate failed");
        errnum = errno;
        goto out;
    }
    errnum = 0;
out:
    flux_future_destroy (f);
    if (flux_respond (h, msg, errnum, NULL) < 0)
        flux_log_error (h, "reload: flux_respond");
}

static void reload_event_cb (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
{
    resource_ctx_t *ctx = arg;
    const char *nodeset;
    int seq;

    /*  Ignored on rank 0 */
    if (ctx->rank == 0)
        return;

    if (flux_event_unpack (msg, NULL, "{s:s,s:i}",
                           "ranks", &nodeset, "sequence", &seq) < 0) {
        flux_log_error (h, "reload_event_cb: flux_event_unpack");
        return;
    }
    (void) reload_hwloc (h, ctx, nodeset, seq);
}

static void topo_request_cb (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
{
    flux_future_t *df = NULL;
    const flux_kvsdir_t *kd;
    char *buffer = NULL;
    int buflen;
    hwloc_topology_t global = NULL;
    int count = 0;
    int rc = -1;

    if (!(df = flux_kvs_lookup (h, FLUX_KVS_READDIR, "resource.hwloc.xml"))
                || flux_kvs_lookup_get_dir (df, &kd) < 0) {
        flux_log (h, LOG_ERR, "xml dir is not available");
        goto done;
    }
    if (hwloc_topology_init (&global) < 0) {
        flux_log (h, LOG_ERR, "hwloc_topology_init failed");
        goto done;
    }
    hwloc_topology_set_custom (global);

    flux_kvsitr_t *base_iter = flux_kvsitr_create (kd);
    const char *base_key = NULL;
    while ((base_key = flux_kvsitr_next (base_iter))) {
        char *key = flux_kvsdir_key_at (kd, base_key);
        const char *xml = NULL;
        hwloc_topology_t rank;
        flux_future_t *f;

        if (!(f = flux_kvs_lookup (h, 0, key))
                || flux_kvs_lookup_get_unpack (f, "s", &xml) < 0) {
            flux_log_error (h, "%s", base_key);
            flux_future_destroy (f);
            free (key);
            continue;
        }
        if (hwloc_topology_init (&rank) < 0) {
            flux_log_error (h, "%s: hwloc_topology_init", base_key);
            flux_future_destroy (f);
            free (key);
            continue;
        }
        if (hwloc_topology_set_xmlbuffer (rank, xml, strlen (xml)) < 0
                || hwloc_topology_load (rank) < 0
                || hwloc_custom_insert_topology (global,
                                      hwloc_get_root_obj (global),
                                      rank,
                                      NULL) < 0) {
            flux_log_error (h, "%s: hlwoc_set/load/insert", base_key);
            hwloc_topology_destroy (rank);
            continue;
        }
        hwloc_topology_destroy (rank);
        flux_future_destroy (f);
        free (key);
        count++;
    }

    flux_kvsitr_destroy (base_iter);

    hwloc_topology_load (global);
    if (hwloc_topology_export_xmlbuffer (global, &buffer, &buflen) < 0) {
        flux_log (h, LOG_ERR, "error hwloc_topology_export_xmlbuffer");
        goto done;
    }

    /* hwloc_topology_export_xmlbuffer() can be inconsistent on
     * whether or not the buffer includes the NUL char or not.
     * Obviously, we don't want to pass in string to jansson that may
     * not be NUL terminated.
     *
     * We could use string with len argument (i.e. 's#').  However,
     * jansson library may be inconsistent on whether len should
     * contain NUL char.
     *
     * So we modify len argument to based on first non-NUL char at end
     * of string.
     */
    while (buflen) {
        if (buffer[buflen - 1] == '\0')
            buflen--;
        else
            break;
    }

    if (flux_respond_pack (h, msg, "{ s:s# }",
                "topology", buffer, buflen) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto done;
    }
    rc = 0;
done:
    if (buffer)
        hwloc_free_xmlbuffer (global, buffer);
    if (rc < 0) {
        if (flux_respond (h, msg, errno, NULL) < 0)
            flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    }
    if (global)
        hwloc_topology_destroy (global);
    flux_future_destroy (df);
}

static void process_args (flux_t *h, resource_ctx_t *ctx, int argc, char **argv)
{
    int i;
    for (i = 0; i < argc; i++) {
        flux_log (h, LOG_ERR, "Unknown option: %s\n", argv[i]);
    }
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "resource-hwloc.reload", reload_request_cb,
       FLUX_ROLE_OWNER
    },
    { FLUX_MSGTYPE_EVENT,   "resource-hwloc.reload", reload_event_cb,
       FLUX_ROLE_OWNER
    },
    { FLUX_MSGTYPE_REQUEST, "resource-hwloc.topo", topo_request_cb,
       FLUX_ROLE_USER
    },
    FLUX_MSGHANDLER_TABLE_END
};

int mod_main (flux_t *h, int argc, char **argv)
{
    int rc = -1;
    resource_ctx_t *ctx;
    flux_msg_handler_t **handlers = NULL;

    if (!(ctx = resource_hwloc_ctx_create (h)))
        goto done;

    process_args (h, ctx, argc, argv);

    // Load hardware information immediately
    if ((load_hwloc (h, ctx) < 0)
        || (aggregate_push_rank_info (h, ctx, 0) < 0))
        goto done;

    // Wait for aggregate information from all ranks on rank 0
    if (ctx->rank == 0 && (reload_event_wait (h, ctx) < 0)) {
        flux_log_error (h, "reload_event_wait");
        goto done;
    }

    if (flux_event_subscribe (h, "resource-hwloc.reload") < 0) {
        flux_log_error (h, "flux_event_subscribe");
        goto done;
    }

    if (flux_msg_handler_addvec (h, htab, ctx, &handlers) < 0) {
        flux_log_error (h, "flux_msghandler_add");
        goto done;
    }

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done;
    }
    rc = 0;
done:
    flux_msg_handler_delvec (handlers);
    resource_hwloc_ctx_destroy (ctx);
    return rc;
}

MOD_NAME ("resource-hwloc");

/*
 * vi: ts=4 sw=4 expandtab
 */
