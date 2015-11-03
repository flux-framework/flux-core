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

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"
#include "src/modules/libmrpc/mrpc.h"

typedef struct
{
    hwloc_topology_t topology;
    bool loaded;
} ctx_t;

int try_hwloc_load (ctx_t *ctx, const char *const path)
{
    hwloc_topology_set_flags (ctx->topology, HWLOC_TOPOLOGY_FLAG_WHOLE_IO);
    if (path) {
        // load my structure from the hwloc xml file at this path
        hwloc_topology_set_xml (ctx->topology, path);
    }
    return hwloc_topology_load (ctx->topology);
}

static ctx_t *getctx (flux_t h)
{
    flux_conf_t cf = flux_conf_create ();
    if (kvs_conf_load (h, cf) < 0)
        err_exit ("could not load config from KVS");

    uint32_t rank;
    if (flux_get_rank (h, &rank) < 0) {
        err_exit ("flux_get_rank");
    }

    ctx_t *ctx = xzmalloc (sizeof(ctx_t));
    hwloc_topology_init (&ctx->topology);

    char *conf_path = xasprintf ("resource.hwloc.xml.%" PRIu32, rank);
    const char *path = flux_conf_get (cf, conf_path);
    free (conf_path);

    if (!path)
        path = flux_conf_get (cf, "resource.hwloc.default_xml");

    fprintf(stderr, "rank %u reading from conf %s\n", rank, path);
    if (path) {
        if (try_hwloc_load (ctx, path) == 0) {
            return ctx;
        } else {
            err_exit ("hwloc load failed for specified path");
        }
    }

    if (try_hwloc_load (ctx, NULL) < 0)
        err_exit ("hwloc failed to load topology");

    ctx->loaded = false;
    return ctx;
}

#if 0
static void freectx(ctx_t * ctx)
{
    hwloc_topology_destroy(ctx->topology);
    free(ctx);
}
#endif

/* Copy input arguments to output arguments and respond to RPC.
*/
static void query_cb (flux_t h,
                      flux_msg_handler_t *watcher,
                      const flux_msg_t *msg,
                      void *arg)
{
    flux_log (h, LOG_ERR, "UNIMPLEMENTED: %s", __FUNCTION__);
}

static void get_cb (flux_t h,
                    flux_msg_handler_t *watcher,
                    const flux_msg_t *msg,
                    void *arg)
{
    flux_log (h, LOG_ERR, "UNIMPLEMENTED: %s", __FUNCTION__);
}

static int load_xml_to_kvs (flux_t h, ctx_t *ctx)
{
    char *xml_path = NULL;
    char *buffer = NULL;
    int buflen = 0, ret = -1;
    uint32_t rank;

    if (flux_get_rank (h, &rank) < 0) {
        flux_log (h, LOG_ERR, "flux_get_rank");
        goto done;
    }
    xml_path = xasprintf ("resource.hwloc.xml.%" PRIu32, rank);
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
    uint32_t rank;

    if (flux_get_rank (h, &rank) < 0) {
        flux_log (h, LOG_ERR, "flux_get_rank");
        goto done;
    }
    base_path = xasprintf ("resource.hwloc.by_rank.%" PRIu32, rank);
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
        if (walk_topology (h,
                           ctx->topology,
                           hwloc_get_root_obj (ctx->topology),
                           host_path) < 0) {
            flux_log (h, LOG_ERR, "walk_topology");
            goto done;
        }
    }
    ret = 0;
done:
    if (base_path)
        free (base_path);
    return ret;
}

static void load_cb (flux_t h,
                     flux_msg_handler_t *watcher,
                     const flux_msg_t *msg,
                     void *arg)
{
    uint32_t rank, size;
    ctx_t *ctx = (ctx_t *)arg;

    if (load_xml_to_kvs (h, ctx) < 0 || load_info_to_kvs (h, ctx) < 0) {
        return;
    }
    if (flux_get_rank (h, &rank) < 0 || flux_get_size (h, &size) < 0) {
        flux_log (h, LOG_ERR, "flux_get_rank/size: %s", strerror (errno));
        return;
    }
    char *completion_path = xasprintf ("resource.hwloc.loaded.%" PRIu32, rank);
    kvs_put_int (h, completion_path, 1);
    free (completion_path);

    kvs_fence (h, "resource_hwloc_loaded", size);

    flux_log (h, LOG_DEBUG, "loaded");

    ctx->loaded = true;
}

static void topo_cb (flux_t h,
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
        kvsdir_get_string (kd, base_key, &xml);

        hwloc_topology_init (&rank);
        hwloc_topology_set_xmlbuffer (rank, xml, strlen (xml));
        hwloc_topology_load (rank);
        hwloc_custom_insert_topology (global,
                                      hwloc_get_root_obj (global),
                                      rank,
                                      NULL);
        hwloc_topology_destroy (rank);
        free (xml);

        flux_log (h, LOG_INFO, "resource_hwloc: loaded from %s", base_key);
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

    flux_respond (h, msg, 0, Jtostr (out));

done:
    hwloc_topology_destroy (global);
    Jput (out);
}

static void start_all (flux_t h, ...)
{
    flux_msg_handler_t *w;
    va_list ap;
    va_start (ap, h);
    while ((w = va_arg (ap, flux_msg_handler_t *))) {
        flux_msg_handler_start (w);
    }
}

struct
{
    flux_msg_handler_t *load;
    flux_msg_handler_t *query;
    flux_msg_handler_t *get;
    flux_msg_handler_t *topo;
    flux_msg_handler_t *END;
} handlers = {};

int mod_main (flux_t h, int argc, char **argv)
{
    ctx_t *ctx = getctx (h);

    // Load hardware information immediately
    load_cb (h, 0, NULL, ctx);

    if (flux_event_subscribe (h, "resource-hwloc.load") < 0) {
        flux_log (h, LOG_ERR, "%s: flux_event_subscribe", __FUNCTION__);
        return -1;
    }

    handlers.load = flux_msg_handler_create (h, FLUX_MATCH_EVENT, load_cb, ctx);
    flux_msg_handler_start (handlers.load);
    handlers.query =
        flux_msg_handler_create (h, FLUX_MATCH_REQUEST, query_cb, ctx);
    flux_msg_handler_start (handlers.query);
    handlers.get = flux_msg_handler_create (h, FLUX_MATCH_REQUEST, get_cb, ctx);
    flux_msg_handler_start (handlers.get);
    handlers.topo =
        flux_msg_handler_create (h, FLUX_MATCH_REQUEST, topo_cb, ctx);
    flux_msg_handler_start (handlers.topo);

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_run: %s", strerror (errno));
        return -1;
    }

    flux_msg_handler_destroy (handlers.load);
    flux_msg_handler_destroy (handlers.query);
    flux_msg_handler_destroy (handlers.get);
    flux_msg_handler_destroy (handlers.topo);

    return 0;
}

MOD_NAME ("resource-hwloc");

/*
 * vi: ts=4 sw=4 expandtab
 */
