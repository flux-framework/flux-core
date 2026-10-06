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
 * Values are translated generically by message.c (see RFC 52), but
 * method call signatures are not carried in requests, so each
 * (interface, member) that we call from Flux must be listed here.
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

#include "message.h"
#include "interface.h"

/* Method call signatures, needed to translate JSON requests to D-Bus.
 * Replies and signals are self-describing and need no table.
 */
struct xtab {
    const char *member;
    const char *signature;
};

/* Manager methods
 */
static const struct xtab managertab[] = {
    { "Subscribe",              "" },
    { "Unsubscribe",            "" },
    { "ListUnitsByPatterns",    "asas" },
    { "KillUnit",               "ssi" },
    { "StopUnit",               "ss" },
    { "ResetFailedUnit",        "s" },
    { "StartTransientUnit",     "ssa(sv)a(sa(sv))" },
    { "GetUnitByInvocationID",  "ay" },
};

static const struct xtab dbustab[] = {
    { "AddMatch",               "s" },
    { "RemoveMatch",            "s" },
};

static const struct xtab proptab[] = {
    { "GetAll",                 "s" },
    { "Get",                    "ss" },
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
    const char *path = "/org/freedesktop/systemd1";
    const char *interface = "org.freedesktop.systemd1.Manager";
    const char *member;
    const struct xtab *x;
    sd_bus_message *m;
    int e;

    if (json_unpack (obj,
                     "{s?s s?s s?s s:s s:o}",
                     "destination", &destination,
                     "path", &path,
                     "interface", &interface,
                     "member", &member,
                     "params", &params) < 0
        || !json_is_array (params)) {
        errprintf (error, "malformed request");
        return NULL;
    }
    if (!(x = xtab_lookup (interface, member, error)))
        return NULL;
    if ((e = sd_bus_message_new_method_call (bus,
                                             &m,
                                             destination,
                                             path,
                                             interface,
                                             member)) < 0) {
        errprintf (error, "error creating sd-bus message: %s", strerror (-e));
        return NULL;
    }
    if ((e = sdmsg_write (m, x->signature, params)) < 0) {
        errprintf (error,
                   "error translating JSON to %s method-call: %s",
                   x->member,
                   strerror (-e));
        sd_bus_message_unref (m);
        return NULL;
    }
    return m;
}

json_t *interface_reply_tojson (sd_bus_message *m, flux_error_t *error)
{
    json_t *o;
    int e;

    if (!(o = json_pack ("{s:[]}", "params"))) {
        errprintf (error, "error creating output parameter object");
        return NULL;
    }
    if ((e = sdmsg_read (m, json_object_get (o, "params"))) < 0) {
        errprintf (error,
                   "error translating %s method-return to JSON: %s",
                   sd_bus_message_get_member (m),
                   strerror (-e));
        json_decref (o);
        return NULL;
    }
    return o;
}

json_t *interface_signal_tojson (sd_bus_message *m, flux_error_t *error)
{
    const char *iface = sd_bus_message_get_interface (m);
    const char *member = sd_bus_message_get_member (m);
    const char *path = sd_bus_message_get_path (m);
    json_t *o;
    int e;

    if (!iface || !member || !path) {
        errprintf (error, "signal is missing interface, member, or path");
        return NULL;
    }
    if (!(o = json_pack ("{s:s s:s s:s s:[]}",
                         "path", path,
                         "interface", iface,
                         "member", member,
                         "params"))) {
        errprintf (error, "error creating output parameter object");
        return NULL;
    }
    if ((e = sdmsg_read (m, json_object_get (o, "params"))) < 0) {
        errprintf (error,
                   "error translating %s signal to JSON: %s",
                   member,
                   strerror (-e));
        json_decref (o);
        return NULL;
    }
    return o;
}

// vi:ts=4 sw=4 expandtab
