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

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"
#include "src/modules/kvs/kvs.h"

typedef struct
{
    uint32_t rank;
    hwloc_topology_t topology;
    bool loaded;
} ctx_t;

static int ctx_hwloc_init (flux_t h, ctx_t *ctx)
{
    int ret = -1;
    char *key, *path = NULL;
    hwloc_bitmap_t restrictset = NULL;
    uint32_t hwloc_version;

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
    (void)kvs_get_string (h, key, &path);
    free (key);
    if (!path)
        (void)kvs_get_string (h, "config.resource.hwloc.default_xml", &path);

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
    if (path)
        free (path);
    if (restrictset)
        hwloc_bitmap_free (restrictset);
    return ret;
}

void freectx (ctx_t *ctx)
{
    if (ctx) {
        if (ctx->topology)
            hwloc_topology_destroy (ctx->topology);
        free (ctx);
    }
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = xzmalloc (sizeof(ctx_t));
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
    freectx (ctx);
    return NULL;
}

void unlink_if_exists (flux_t h, const char *path)
{
    if (!kvs_unlink (h, path))  // if the unlink succeeds
        kvs_commit (h);  // ensure the unlink is committed before proceeding
}

static int load_xml_to_kvs (flux_t h, ctx_t *ctx)
{
    char *xml_path = NULL;
    char *buffer = NULL;
    int buflen = 0, ret = -1;

    xml_path = xasprintf ("resource.hwloc.xml.%" PRIu32, ctx->rank);
    unlink_if_exists (h, xml_path);
    if (hwloc_topology_export_xmlbuffer (ctx->topology, &buffer, &buflen) < 0) {
        flux_log (h, LOG_ERR, "hwloc_topology_export_xmlbuffer");
        goto done;
    }
    if (kvs_put_string (h, xml_path, buffer) < 0) {
        flux_log (h, LOG_ERR, "kvs_put_string");
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
            ret_str = suffix;
        else
            ret_str = xasprintf ("%s.%s", ret_str, suffix);
        free (suffix);
        free (tmp);
    }
    va_end (varargs);
    return ret_str;
}

static int walk_topology (flux_t h,
                          hwloc_topology_t topology,
                          hwloc_obj_t obj,
                          const char *path)
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
    kvs_put_int (h, os_index_path, obj->os_index);

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

                kvs_ret = kvs_put_string (h, value_path, value);

                free (value_path);
                if (kvs_ret < 0)
                    goto done;
            }
        }
    }

    // Recurse into the children of this object
    while ((prev = hwloc_get_next_child (topology, obj, prev))) {
        if (walk_topology (h, topology, prev, new_path) < 0)
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

static int load_info_to_kvs (flux_t h, ctx_t *ctx)
{
    char *base_path = NULL;
    int ret = -1, i;
    int depth = hwloc_topology_get_depth (ctx->topology);

    base_path = xasprintf ("resource.hwloc.by_rank.%" PRIu32, ctx->rank);
    unlink_if_exists (h, base_path);
    for (i = 0; i < depth; ++i) {
        int nobj = hwloc_get_nbobjs_by_depth (ctx->topology, i);
        hwloc_obj_type_t t = hwloc_get_depth_type (ctx->topology, i);
        char *obj_path =
            xasprintf ("%s.%s", base_path, hwloc_obj_type_string (t));
        kvs_put_int (h, obj_path, nobj);
        free (obj_path);
    }
    if (walk_topology (h,
                       ctx->topology,
                       hwloc_get_root_obj (ctx->topology),
                       base_path) < 0) {
        flux_log (h, LOG_ERR, "walk_topology");
        goto done;
    }
    hwloc_obj_t machine =
        hwloc_get_obj_by_type (ctx->topology, HWLOC_OBJ_MACHINE, 0);
    if (machine) {
        const char *hostname = hwloc_obj_get_info_by_name (machine, "HostName");
        char *kvs_hostname = escape_kvs_key (hostname);
        char *host_path = xasprintf ("resource.hwloc.by_host.%s", kvs_hostname);
        free (kvs_hostname);
        unlink_if_exists (h, host_path);
        if (walk_topology (h,
                           ctx->topology,
                           hwloc_get_root_obj (ctx->topology),
                           host_path) < 0) {
            flux_log (h, LOG_ERR, "walk_topology");
            goto done;
        }
        free (host_path);
    }
    ret = 0;
done:
    if (base_path)
        free (base_path);
    return ret;
}

static int load_hwloc (flux_t h, ctx_t *ctx)
{
    uint32_t size;
    char *completion_path = NULL;
    int rc = -1;

    if (load_xml_to_kvs (h, ctx) < 0 || load_info_to_kvs (h, ctx) < 0) {
        flux_log_error (h, "%s: failed to load xml/info to kvs", __FUNCTION__);
        goto done;
    }
    if (flux_get_size (h, &size) < 0) {
        flux_log_error (h, "%s: flux_get_size", __FUNCTION__);
        goto done;
    }
    completion_path = xasprintf ("resource.hwloc.loaded.%" PRIu32, ctx->rank);
    if (kvs_put_int (h, completion_path, 1) < 0) {
        flux_log_error (h, "%s: kvs_put_int", __FUNCTION__);
        goto done;
    }
    if (kvs_commit (h) < 0) {
        flux_log_error (h, "%s: kvs_commit", __FUNCTION__);
        goto done;
    }
    flux_log (h, LOG_DEBUG, "loaded");
    ctx->loaded = true;
    rc = 0;
done:
    if (completion_path)
        free (completion_path);
    return rc;
}

static void load_event_cb (flux_t h,
                           flux_msg_handler_t *watcher,
                           const flux_msg_t *msg,
                           void *arg)
{
    ctx_t *ctx = arg;
    (void)load_hwloc (h, ctx);
}

static void reload_request_cb (flux_t h,
                               flux_msg_handler_t *watcher,
                               const flux_msg_t *msg,
                               void *arg)
{
    ctx_t *ctx = arg;
    int errnum = 0;

    if (ctx_hwloc_init (h, ctx) < 0 || load_hwloc (h, ctx) < 0)
        errnum = errno;
    if (flux_respond (h, msg, errnum, "{}") < 0)
        flux_log_error (h, "flux_respond");
}

static void topo_request_cb (flux_t h,
                             flux_msg_handler_t *watcher,
                             const flux_msg_t *msg,
                             void *arg)
{
    ctx_t *ctx = (ctx_t *)arg;
    kvsdir_t *kd = NULL;
    json_object *out = NULL;
    char *buffer;
    int buflen;
    hwloc_topology_t global;
    hwloc_topology_init (&global);
    int count = 0;
    uint32_t size;

    if (flux_get_size (h, &size) < 0) {
        flux_respond (h, msg, errno, NULL);
        flux_log_error (h, "%s: flux_get_size", __FUNCTION__);
        goto done;
    }
    if (!ctx->loaded) {
        flux_log (h,
                  LOG_ERR,
                  "topology cannot be aggregated, it has not been loaded");
        flux_respond (h, msg, EINVAL, NULL);
        goto done;
    }

    if (kvs_get_dir (h, &kd, "resource.hwloc.xml") < 0) {
        flux_log (h, LOG_ERR, "xml dir is not available");
        flux_respond (h, msg, errno, NULL);
        goto done;
    }

    hwloc_topology_set_custom (global);

    kvsitr_t *base_iter = kvsitr_create (kd);
    const char *base_key = NULL;
    while ((base_key = kvsitr_next (base_iter))) {
        char *xml = NULL;
        hwloc_topology_t rank;
        if (kvsdir_get_string (kd, base_key, &xml) < 0) {
            flux_log_error (h, "%s", base_key);
            continue;
        }
        if (hwloc_topology_init (&rank) < 0) {
            flux_log_error (h, "%s: hwloc_topology_init", base_key);
            free (xml);
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
            free (xml);
            continue;
        }
        hwloc_topology_destroy (rank);
        free (xml);

        flux_log (h, LOG_DEBUG, "%s: loaded", base_key);
        count++;
    }

    kvsitr_destroy (base_iter);
    kvsdir_destroy (kd);

    out = Jnew ();

    hwloc_topology_load (global);
    if (hwloc_topology_export_xmlbuffer (global, &buffer, &buflen) < 0) {
        flux_respond (h, msg, errno, NULL);
        goto done;
    }
    Jadd_str (out, "topology", buffer);
    hwloc_free_xmlbuffer (global, buffer);
    if (count < size) {
        flux_log (h, LOG_ERR, "only got %d out of %d ranks aggregated",
                count, size);
        flux_respond (h, msg, EAGAIN, NULL);
    } else
        flux_respond (h, msg, 0, Jtostr (out));
done:
    hwloc_topology_destroy (global);
    Jput (out);
}

static struct flux_msg_handler_spec htab[] = {
    {FLUX_MSGTYPE_EVENT, "resource-hwloc.load", load_event_cb, NULL},
    {FLUX_MSGTYPE_REQUEST, "resource-hwloc.reload", reload_request_cb, NULL},
    {FLUX_MSGTYPE_REQUEST, "resource-hwloc.topo", topo_request_cb, NULL},
    FLUX_MSGHANDLER_TABLE_END};

int mod_main (flux_t h, int argc, char **argv)
{
    int rc = -1;
    ctx_t *ctx;

    if (!(ctx = getctx (h)))
        goto done;

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
    freectx (ctx);
    return rc;
}

MOD_NAME ("resource-hwloc");

/*
 * vi: ts=4 sw=4 expandtab
 */
