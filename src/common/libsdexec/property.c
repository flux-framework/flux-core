/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* property.c - query unit properties
 *
 * The Get method-reply includes a single property value, represented as a
 * D-Bus variant type, which is a (type, value) tuple: [s,o]
 *
 * The GetAll method-reply and the PropertiesChanged signal include a
 * dictionary of property values:
 * {s:[s,o], s:[s,o], s:[s,o], ...}
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "ccan/str/str.h"
#include "src/common/libutil/errno_safe.h"

#include "property.h"

static const char *serv_interface = "org.freedesktop.systemd1.Service";
static const char *prop_interface = "org.freedesktop.DBus.Properties";

flux_future_t *sdexec_property_get_all (flux_t *h,
                                        const char *service,
                                        uint32_t rank,
                                        const char *path)
{
    flux_future_t *f;
    char topic[256];

    if (!h || !service || !path) {
        errno = EINVAL;
        return NULL;
    }
    snprintf (topic, sizeof (topic), "%s.call", service);
    if (!(f = flux_rpc_pack (h,
                             topic,
                             rank,
                             0,
                             "{s:s s:s s:s s:[s]}",
                             "path", path,
                             "interface", prop_interface,
                             "member", "GetAll",
                             "params", serv_interface)))
        return NULL;
    return f;
}

flux_future_t *sdexec_property_get (flux_t *h,
                                    const char *service,
                                    uint32_t rank,
                                    const char *path,
                                    const char *name)
{
    flux_future_t *f;
    char topic[256];

    if (!h || !service || !path || !name) {
        errno = EINVAL;
        return NULL;
    }
    snprintf (topic, sizeof (topic), "%s.call", service);
    if (!(f = flux_rpc_pack (h,
                             "sdbus.call",
                             rank,
                             0,
                             "{s:s s:s s:s s:[ss]}",
                             "path", path,
                             "interface", prop_interface,
                             "member", "Get",
                             "params", serv_interface, name)))
        return NULL;
    return f;
}

flux_future_t *sdexec_property_changed (flux_t *h,
                                        const char *service,
                                        uint32_t rank,
                                        const char *path)
{
    flux_future_t *f;
    json_t *o;
    char topic[256];

    if (!h || !service) {
        errno = EINVAL;
        return NULL;
    }
    if (!(o = json_pack ("{s:s s:s}",
                         "interface", prop_interface,
                         "member", "PropertiesChanged")))
        goto nomem;
    if (path) {
        json_t *val = json_string (path);
        if (!val || json_object_set_new (o, "path", val) < 0) {
            json_decref (val);
            goto nomem;
        }
    }
    snprintf (topic, sizeof (topic), "%s.subscribe", service);
    if (!(f = flux_rpc_pack (h,
                             topic,
                             rank,
                             FLUX_RPC_STREAMING,
                             "O", o)))
        goto error;
    json_decref (o);
    return f;
nomem:
    errno = ENOMEM;
error:
    ERRNO_SAFE_WRAP (json_decref, o);
    return NULL;
}

int sdexec_property_get_unpack (flux_future_t *f, const char *fmt, ...)
{
    const char *type; // ignored
    json_t *val;
    va_list ap;
    int rc;

    if (!f || !fmt) {
        errno = EINVAL;
        return -1;
    }
    if (flux_rpc_get_unpack (f, "{s:[[so]]}", "params", &type, &val) < 0)
        return -1;
    va_start (ap, fmt);
    rc = json_vunpack_ex (val, NULL, 0, fmt, ap);
    va_end (ap);
    if (rc < 0) {
        errno = EPROTO;
        return -1;
    }
    return rc;
}

int sdexec_property_dict_unpack (json_t *dict,
                                 const char *name,
                                 const char *fmt,
                                 ...)

{
    const char *type; // ignored
    json_t *val;
    va_list ap;
    int rc;

    if (!dict || !name || !fmt) {
        errno = EINVAL;
        return -1;
    }
    if (json_unpack (dict, "{s:[so]}", name, &type, &val) < 0) {
        errno = EPROTO;
        return -1;
    }
    va_start (ap, fmt);
    rc = json_vunpack_ex (val, NULL, 0, fmt, ap);
    va_end (ap);
    if (rc < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

json_t *sdexec_property_get_all_dict (flux_future_t *f)

{
    json_t *dict;

    if (flux_rpc_get_unpack (f,
                             "{s:[o]}",
                             "params", &dict) < 0)
        return NULL;
    return dict;
}

json_t *sdexec_property_changed_dict (flux_future_t *f)
{
    const char *iface;
    json_t *dict;
    json_t *inval;

    if (flux_rpc_get_unpack (f,
                             "{s:[s o o]}",
                             "params", &iface, &dict, &inval) < 0)
        return NULL;
    return dict;
}

const char *sdexec_property_changed_path (flux_future_t *f)
{
    const char *path;

    if (flux_rpc_get_unpack (f, "{s:s}", "path", &path) < 0)
        return NULL;
    return path;
}

// vi:ts=4 sw=4 expandtab
