/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* interface.c - D-Bus message translation to/from JSON
 *
 * This unfortunately falls short of a generic implementation, so each
 * D-Bus (interface, member) that we need in Flux requires translation
 * callbacks here for now.
 *
 * To list systemd Manager methods and signatures:
 *   busctl --user introspect \
 *      org.freedesktop.systemd1 \
 *      /org/freedesktop/systemd1 \
 *      org.freedesktop.systemd1.Manager
 *
 * dbus-monitor(1) is a useful debugging tool.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <errno.h>
#include <systemd/sd-bus.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"
#include "ccan/array_size/array_size.h"

#include "objpath.h"
#include "message.h"
#include "interface.h"

typedef int (*fromjson_f)(sd_bus_message *m, const char *sig, json_t *param);
typedef int (*tojson_f)(sd_bus_message *m, const char *sig, json_t *param);

struct xtab {
    const char *member;
    const char *fromjson_sig;
    fromjson_f fromjson;
    const char *tojson_sig;
    tojson_f tojson;
};

static int generic_fromjson (sd_bus_message *m,
                             const char *sig,
                             json_t *param)
{
    return sdmsg_write (m, sig, param);
}

static int generic_tojson (sd_bus_message *m,
                           const char *sig,
                           json_t *params)
{
    return sdmsg_read (m, sig, params);
}

static int list_units_tojson (sd_bus_message *m,
                              const char *sig,
                              json_t *params)
{
    int e;
    json_t *a;

    if (!(a = json_array ()))
        return -ENOMEM;
    if ((e = sd_bus_message_enter_container (m, 'a', "(ssssssouso)")) <= 0)
        goto out;
    while ((e = sd_bus_message_enter_container (m, 'r', "ssssssouso")) > 0) {
        json_t *entry;
        if (!(entry = json_array ())) {
            e = -ENOMEM;
            goto out;
        }
        if ((e = sdmsg_read (m, "ssssssouso", entry)) <= 0) {
            if (e == 0)
                e = -EPROTO;
            json_decref (entry);
            goto out;
        }
        if (json_array_append_new (a, entry) < 0) {
            json_decref (entry);
            e = -ENOMEM;
            goto out;
        }
        if ((e = sd_bus_message_exit_container (m)) < 0)
            goto out;
    }
    if (e < 0 || (e = sd_bus_message_exit_container (m)) < 0)
        goto out;
    if (json_array_append_new (params, a) < 0) {
        e = -ENOMEM;
        goto out;
    }
    return 1;
out:
    json_decref (a);
    return e;
}

/* This is currently unused in flux so aux is required to be an empty array.
 */
static int add_aux_units (sd_bus_message *m, json_t *aux)
{
    if (!json_is_array (aux) || json_array_size (aux) > 0)
        return -EPROTO;
    return sd_bus_message_append (m, "a(sa(sv))", 0);
}

// s s a(sv) a(sa(sv))
static int start_transient_unit_fromjson (sd_bus_message *m,
                                          const char *sig,
                                          json_t *params)
{
    const char *name;
    const char *mode;
    json_t *props;
    json_t *aux;
    int e;

    if (json_unpack (params, "[ssoo]", &name, &mode, &props, &aux) < 0)
        return -EPROTO;
    if ((e = sd_bus_message_append (m, "s", name)) < 0
        || (e = sd_bus_message_append (m, "s", mode)) < 0
        || (e = sdmsg_put (m, "a(sv)", props)) < 0
        || (e = add_aux_units (m, aux)) < 0)
        return e;
    return 0;
}

/* Manager methods
 */
static const struct xtab managertab[] = {
    { "Subscribe",
      "",       NULL,
      "",       NULL,
    },
    { "Unsubscribe",
      "",       NULL,
      "",       NULL,
    },
    { "ListUnitsByPatterns",
      "asas",           generic_fromjson,
      "a(ssssssouso)",  list_units_tojson,
    },
    { "KillUnit",
      "ssi",    generic_fromjson,
      "",       NULL,
    },
    { "StopUnit",
      "ss",     generic_fromjson,
      "o",      generic_tojson,
    },
    { "ResetFailedUnit",
      "s",      generic_fromjson,
      "",       NULL,
    },
    { "StartTransientUnit",
      "ssa(sv)a(sa(sv))",   start_transient_unit_fromjson,
      "o",                  generic_tojson,
    },
};

static const struct xtab dbustab[] = {
    { "AddMatch",
      "s",      generic_fromjson,
      "",       NULL
    },
    { "RemoveMatch",
      "s",      generic_fromjson,
      "",       NULL
    },
};

static const struct xtab proptab[] = {
    { "GetAll",
      "s",      generic_fromjson,
      "a{sv}",  generic_tojson
    },
    { "Get",
      "ss",     generic_fromjson,
      "v",      generic_tojson,
    },
    // signal
    { "PropertiesChanged",
      "",       NULL,
      "sa{sv}as",generic_tojson
    },
};


static const struct xtab *xtab_lookup (const char *interface,
                                       const char *member,
                                       flux_error_t *error)
{
    const struct xtab *tab = NULL;
    size_t size = 0;

    if (interface) {
        if (streq (interface, "org.freedesktop.systemd1.Manager")) {
            tab = managertab;
            size = ARRAY_SIZE (managertab);
        }
        else if (streq (interface, "org.freedesktop.DBus")) {
            tab = dbustab;
            size = ARRAY_SIZE (dbustab);
        }
        else if (streq (interface, "org.freedesktop.DBus.Properties")) {
            tab = proptab;
            size = ARRAY_SIZE (proptab);
        }
    }
    if (!tab) {
        errprintf (error, "unknown interface %s", interface);
        return NULL;
    }
    if (member) {
        for (int i = 0; i < size; i++) {
            if (streq (tab[i].member, member))
                return &tab[i];
        }
    }
    errprintf (error, "unknown member %s of interface %s", member, interface);
    return NULL;
}

sd_bus_message *interface_request_fromjson (sd_bus *bus,
                                            json_t *obj,
                                            flux_error_t *error)
{
    json_t *params;
    const char *destination = "org.freedesktop.systemd1";
    const char *xpath = "/org/freedesktop/systemd1";
    const char *interface = "org.freedesktop.systemd1.Manager";
    const char *member;
    char *path = NULL;
    const struct xtab *x;
    sd_bus_message *m;
    int e;

    if (json_unpack (obj,
                     "{s?s s?s s?s s:s s:o}",
                     "destination", &destination,
                     "path", &xpath,
                     "interface", &interface,
                     "member", &member,
                     "params", &params) < 0
        || !json_is_array (params)) {
        errprintf (error, "malformed request");
        return NULL;
    }
    if (!(x = xtab_lookup (interface, member, error)))
        return NULL;
    if (!(path = objpath_encode (xpath))) {
        errprintf (error, "error encoding object path %s", xpath);
        return NULL;
    }
    if ((e = sd_bus_message_new_method_call (bus,
                                             &m,
                                             destination,
                                             path,
                                             interface,
                                             member)) < 0) {
        errprintf (error, "error creating sd-bus message: %s", strerror (-e));
        free (path);
        return NULL;
    }
    if (x->fromjson) {
        if ((e = x->fromjson (m, x->fromjson_sig, params)) < 0) {
            errprintf (error,
                       "error translating JSON to %s method-call: %s",
                       x->member,
                       strerror (-e));
            sd_bus_message_unref (m);
            free (path);
            return NULL;
        }
    }
    free (path);
    return m;
}

json_t *interface_reply_tojson (sd_bus_message *m,
                                const char *interface,
                                const char *member,
                                flux_error_t *error)
{
    const struct xtab *x;
    json_t *o;
    json_t *params;
    int e;

    if (!(x = xtab_lookup (interface, member, error)))
        return NULL;
    if (!(o = json_pack ("{s:[]}", "params"))) {
        errprintf (error, "error creating output parameter object");
        return NULL;
    }
    params = json_object_get (o, "params");
    if (x->tojson) {
        if ((e = x->tojson (m, x->tojson_sig, params)) <= 0) {
            if (e == 0)
                e = -EPROTO;
            errprintf (error,
                       "error translating %s method-return to JSON: %s",
                       x->member,
                       strerror (-e));
            json_decref (o);
            return NULL;
        }
    }
    return o;
}

json_t *interface_signal_tojson (sd_bus_message *m, flux_error_t *error)
{
    const char *iface = sd_bus_message_get_interface (m);
    const char *member = sd_bus_message_get_member (m);
    const char *path = sd_bus_message_get_path (m);
    char *xpath;
    const struct xtab *x;
    json_t *o = NULL;
    json_t *params;
    int e;

    if (!(x = xtab_lookup (iface, member, error)))
        return NULL;
    if (!(xpath = objpath_decode (path))) {
        errprintf (error, "error decoding object path %s", path);
        return NULL;
    }
    if (!(o = json_pack ("{s:s s:s s:s s:[]}",
                         "path", xpath,
                         "interface", iface,
                         "member", member,
                         "params"))) {
        errprintf (error, "error creating output parameter object");
        free (xpath);
        return NULL;
    }
    params = json_object_get (o, "params");
    if (x->tojson) {
        if ((e = x->tojson (m, x->tojson_sig, params)) <= 0) {
            if (e == 0)
                e = -EPROTO;
            errprintf (error,
                       "error translating %s signal to JSON: %s",
                       x->member,
                       strerror (-e));
            free (o);
            free (xpath);
            return NULL;
        }
    }
    free (xpath);
    return o;
}

// vi:ts=4 sw=4 expandtab
