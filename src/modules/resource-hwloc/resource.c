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

#include <flux/core.h>

#include <stdarg.h>
#include <hwloc.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <inttypes.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"

typedef struct
{
    uint32_t rank;
    hwloc_topology_t topology;
    bool loaded;
    bool walk_topology;
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
                                HWLOC_TOPOLOGY_FLAG_WHOLE_IO) < 0) {
        flux_log_error (h, "hwloc_topology_set_flags");
        goto done;
    }

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
    ctx->loaded = false;
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

static char *escape_kvs_key (const char *key)
{
    char *ret_str = key ? xstrdup (key) : NULL;
    char *s;
    for (s = ret_str; s && *s; s++)
        if (*s == '.')
            *s = ':';
    return ret_str;
}

static char *escape_and_join_kvs_path (const char *base,
                                       int64_t num_suffixes,
                                       ...)
{
    int64_t i;
    va_list varargs;
    char *ret_str = base ? xstrdup (base) : NULL;
    va_start (varargs, num_suffixes);
    for (i = 0; i < num_suffixes; i++) {
        char *tmp = ret_str;
        char *suffix = va_arg (varargs, char *);
        if (!suffix || !strlen (suffix))
            continue;

        suffix = escape_kvs_key (suffix);

        if (!ret_str || !strlen (ret_str))
            ret_str = xstrdup (suffix);
        else
            ret_str = xasprintf ("%s.%s", ret_str, suffix);
        free (suffix);
        free (tmp);
    }
    va_end (varargs);
    return ret_str;
}

static int walk_topology (flux_t *h,
                          hwloc_topology_t topology,
                          hwloc_obj_t obj,
                          const char *path,
                          flux_kvs_txn_t *txn)
{
    int ret = -1;
    int size_buf = hwloc_obj_attr_snprintf (NULL, 0, obj, ":-!:", 1) + 1;
    int size_type = hwloc_obj_type_snprintf (NULL, 0, obj, 1) + 1;
    char *os_index_path, *new_path = NULL, *token = NULL, *end = NULL;
    char *buf = xzmalloc (size_buf);
    char *type = xzmalloc (size_type);
    hwloc_obj_t prev = NULL;

    hwloc_obj_attr_snprintf (buf, size_buf, obj, ":-!:", 1);
    hwloc_obj_type_snprintf (type, size_type, obj, 1);

    new_path = xasprintf ("%s.%s_%u", path, type, obj->logical_index);

    os_index_path = xasprintf ("%s.os_index", new_path);
    if (flux_kvs_txn_pack (txn, 0, os_index_path, "i", obj->os_index) < 0) {
        flux_log_error (h, "%s: flux_kvs_txn_pack", __FUNCTION__);
        goto done;
    }

    // Tokenize the string, break out key/value pairs and store appropriately
    for (token = buf, end = strstr (token, ":-!:"); end && token;
         token = end + 4, end = strstr (token, ":-!:")) {
        end[0] = '\0';
        char *value = strstr (token, "=");
        if (value) {
            value[0] = '\0';
            {
                char *value_path =
                    escape_and_join_kvs_path (new_path, 1, token);
                int kvs_ret = -1;
                value++;

                kvs_ret = flux_kvs_txn_pack (txn, 0, value_path, "s", value);

                free (value_path);
                if (kvs_ret < 0) {
                    flux_log_error (h, "%s: flux_kvs_txn_pack", __FUNCTION__);
                    goto done;
                }
            }
        }
    }

    // Recurse into the children of this object
    while ((prev = hwloc_get_next_child (topology, obj, prev))) {
        if (walk_topology (h, topology, prev, new_path, txn) < 0)
            goto done;
    }

    ret = 0;
done:
    free (os_index_path);
    free (new_path);
    free (buf);
    free (type);
    return ret;
}

static int put_hostname (flux_t *h, const char *base, const char *hostname,
                         flux_kvs_txn_t *txn)
{
    int rc;
    char *key;
    if (asprintf (&key, "%s.HostName", base) < 0)
        return (-1);
    rc = flux_kvs_txn_pack (txn, 0, key, "s", hostname);
    free (key);
    return (rc);
}

static int load_info_to_kvs (flux_t *h, resource_ctx_t *ctx,
                             flux_kvs_txn_t *txn)
{
    char *base_path = NULL;
    int ret = -1, i;
    int depth = hwloc_topology_get_depth (ctx->topology);

    base_path = xasprintf ("resource.hwloc.by_rank.%" PRIu32, ctx->rank);
    if (flux_kvs_txn_unlink (txn, 0, base_path) < 0) {
        flux_log_error (h, "%s: flux_kvs_unlink", __FUNCTION__);
        goto done;
    }
    for (i = 0; i < depth; ++i) {
        int nobj = hwloc_get_nbobjs_by_depth (ctx->topology, i);
        hwloc_obj_type_t t = hwloc_get_depth_type (ctx->topology, i);
        char *obj_path =
            xasprintf ("%s.%s", base_path, hwloc_obj_type_string (t));
        if (flux_kvs_txn_pack (txn, 0, obj_path, "i", nobj) < 0) {
            flux_log_error (h, "%s: flux_kvs_txn_pack", __FUNCTION__);
            free (obj_path);
            goto done;
        }
        free (obj_path);
    }
    if (ctx->walk_topology &&
        walk_topology (h,
                       ctx->topology,
                       hwloc_get_root_obj (ctx->topology),
                       base_path,
                       txn) < 0) {
        flux_log (h, LOG_ERR, "walk_topology");
        goto done;
    }
    hwloc_obj_t machine =
        hwloc_get_obj_by_type (ctx->topology, HWLOC_OBJ_MACHINE, 0);
    if (machine) {
        const char *hostname = hwloc_obj_get_info_by_name (machine, "HostName");
        char *kvs_hostname = escape_kvs_key (hostname);
        char *host_path = xasprintf ("resource.hwloc.by_host.%s", kvs_hostname);

        if (put_hostname (h, base_path, hostname, txn) < 0) {
            flux_log_error (h, "%s: put_hostname", __FUNCTION__);
            free (kvs_hostname);
            free (host_path);
            goto done;
        }
        free (kvs_hostname);

        if (flux_kvs_txn_unlink (txn, 0, host_path) < 0) {
            flux_log_error (h, "%s: flux_kvs_txn_unlink", __FUNCTION__);
            free (host_path);
            goto done;
        }
        if (ctx->walk_topology &&
            walk_topology (h,
                           ctx->topology,
                           hwloc_get_root_obj (ctx->topology),
                           host_path,
                           txn) < 0) {
            flux_log (h, LOG_ERR, "walk_topology");
            free (host_path);
            goto done;
        }
        free (host_path);
    }
    ret = 0;
done:
    free (base_path);
    return ret;
}

static int load_hwloc (flux_t *h, resource_ctx_t *ctx)
{
    uint32_t size;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;
    char *completion_path = NULL;
    int rc = -1;

    if (!(txn = flux_kvs_txn_create ())) {
        flux_log_error (h, "%s: flux_kvs_txn_create", __FUNCTION__);
        goto done;
    }
    if (load_xml_to_kvs (h, ctx, txn) < 0) {
        flux_log_error (h, "%s: failed to load xml to kvs", __FUNCTION__);
        goto done;
    }
    if (load_info_to_kvs (h, ctx, txn) < 0) {
        flux_log_error (h, "%s: failed to load info to kvs", __FUNCTION__);
        goto done;
    }
    if (flux_get_size (h, &size) < 0) {
        flux_log_error (h, "%s: flux_get_size", __FUNCTION__);
        goto done;
    }
    completion_path = xasprintf ("resource.hwloc.loaded.%" PRIu32, ctx->rank);
    if (flux_kvs_txn_pack (txn, 0, completion_path, "i", 1) < 0) {
        flux_log_error (h, "%s: flux_kvs_txn_pack", __FUNCTION__);
        goto done;
    }
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0) {
        flux_log_error (h, "%s: flux_kvs_commit", __FUNCTION__);
        goto done;
    }
    flux_log (h, LOG_DEBUG, "loaded");
    ctx->loaded = true;
    rc = 0;
done:
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    if (completion_path)
        free (completion_path);
    return rc;
}

static int decode_reload_request (flux_t *h, resource_ctx_t *ctx,
                                  const flux_msg_t *msg)
{
    int walk_topology = ctx->walk_topology;

    if (flux_request_unpack (msg, NULL, "{}") < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        return (-1);
    }

    /*
     *  Set ctx->walk_topology to value in payload, if given.
     */
    if (!flux_request_unpack (msg, NULL, "{ s:b }",
                              "walk_topology", &walk_topology))
        ctx->walk_topology = walk_topology;

    return (0);
}

static void reload_request_cb (flux_t *h,
                               flux_msg_handler_t *watcher,
                               const flux_msg_t *msg,
                               void *arg)
{
    resource_ctx_t *ctx = arg;
    int errnum = 0;

    if ((decode_reload_request (h, ctx, msg) < 0)
        || (ctx_hwloc_init (h, ctx) < 0)
        || (load_hwloc (h, ctx) < 0))
        errnum = errno;
    if (flux_respond (h, msg, errnum, NULL) < 0)
        flux_log_error (h, "flux_respond");
}

static void topo_request_cb (flux_t *h,
                             flux_msg_handler_t *watcher,
                             const flux_msg_t *msg,
                             void *arg)
{
    resource_ctx_t *ctx = (resource_ctx_t *)arg;
    flux_future_t *df = NULL;
    const flux_kvsdir_t *kd;
    char *buffer = NULL;
    int buflen;
    hwloc_topology_t global = NULL;
    int count = 0;
    uint32_t size;
    int rc = -1;

    if (flux_get_size (h, &size) < 0) {
        flux_log_error (h, "%s: flux_get_size", __FUNCTION__);
        goto done;
    }
    if (!ctx->loaded) {
        flux_log (h,
                  LOG_ERR,
                  "topology cannot be aggregated, it has not been loaded");
        errno = EINVAL;
        goto done;
    }
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

        flux_log (h, LOG_DEBUG, "%s: loaded", base_key);
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

    if (count < size) {
        flux_log (h, LOG_ERR, "only got %d out of %d ranks aggregated",
                count, size);
        errno = EAGAIN;
        goto done;
    } else {
        if (flux_respond_pack (h, msg, "{ s:s# }",
                               "topology", buffer, buflen) < 0) {
            flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
            goto done;
        }
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
        if (strcmp (argv[i], "walk_topology") == 0)
            ctx->walk_topology = true;
        else
            flux_log (h, LOG_ERR, "Unknown option: %s\n", argv[i]);
    }
}

static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "resource-hwloc.reload", reload_request_cb,
       0, NULL
    },
    { FLUX_MSGTYPE_REQUEST, "resource-hwloc.topo", topo_request_cb,
       FLUX_ROLE_USER, NULL
    },
    FLUX_MSGHANDLER_TABLE_END
};

int mod_main (flux_t *h, int argc, char **argv)
{
    int rc = -1;
    resource_ctx_t *ctx;

    if (!(ctx = resource_hwloc_ctx_create (h)))
        goto done;

    process_args (h, ctx, argc, argv);

    // Load hardware information immediately
    if (load_hwloc (h, ctx) < 0)
        goto done;

    if (flux_event_subscribe (h, "resource-hwloc.load") < 0) {
        flux_log_error (h, "flux_event_subscribe");
        goto done;
    }

    if (flux_msg_handler_addvec (h, htab, ctx) < 0) {
        flux_log_error (h, "flux_msghandler_add");
        goto done;
    }

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done_delvec;
    }
    rc = 0;
done_delvec:
    flux_msg_handler_delvec (htab);
done:
    resource_hwloc_ctx_destroy (ctx);
    return rc;
}

MOD_NAME ("resource-hwloc");

/*
 * vi: ts=4 sw=4 expandtab
 */
