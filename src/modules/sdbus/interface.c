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
 * Message bodies are translated by message.c as described in RFC 52.
 * Requests carry the method call signature, and replies and signals are
 * self-describing, so no per-method information is needed here.
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

#include "message.h"
#include "interface.h"

sd_bus_message *interface_request_fromjson (sd_bus *bus,
                                            json_t *obj,
                                            flux_error_t *error)
{
    json_t *params;
    const char *destination;
    const char *path;
    const char *interface;
    const char *member;
    const char *signature;
    sd_bus_message *m;
    int e;

    if (json_unpack (obj,
                     "{s:s s:s s:s s:s s:s s:o}",
                     "destination", &destination,
                     "path", &path,
                     "interface", &interface,
                     "member", &member,
                     "signature", &signature,
                     "params", &params) < 0
        || !json_is_array (params)) {
        errprintf (error, "malformed request");
        return NULL;
    }
    if ((e = sd_bus_message_new_method_call (bus,
                                             &m,
                                             destination,
                                             path,
                                             interface,
                                             member)) < 0) {
        errprintf (error, "error creating sd-bus message: %s", strerror (-e));
        return NULL;
    }
    if ((e = sdmsg_write (m, signature, params)) < 0) {
        errprintf (error,
                   "error translating JSON to %s method-call: %s",
                   member,
                   e == -ESRCH
                       ? "file descriptors cannot be passed between processes"
                       : strerror (-e));
        sd_bus_message_unref (m);
        return NULL;
    }
    return m;
}

json_t *interface_reply_tojson (sd_bus_message *m, flux_error_t *error)
{
    const char *signature = sd_bus_message_get_signature (m, true);
    json_t *o;
    int e;

    if (!signature
        || !(o = json_pack ("{s:s s:[]}",
                            "signature", signature,
                            "params"))) {
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
    const char *signature = sd_bus_message_get_signature (m, true);
    json_t *o;
    int e;

    if (!iface || !member || !path || !signature) {
        errprintf (error,
                   "signal is missing interface, member, path, or signature");
        return NULL;
    }
    if (!(o = json_pack ("{s:s s:s s:s s:s s:[]}",
                         "path", path,
                         "interface", iface,
                         "member", member,
                         "signature", signature,
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
