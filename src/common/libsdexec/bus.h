/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBSDEXEC_BUS_H
#define _LIBSDEXEC_BUS_H

/* D-Bus names of the systemd manager, for sdbus.call requests (RFC 52).
 */
#define SDEXEC_DESTINATION      "org.freedesktop.systemd1"
#define SDEXEC_MANAGER_PATH     "/org/freedesktop/systemd1"
#define SDEXEC_MANAGER_IFACE    "org.freedesktop.systemd1.Manager"
#define SDEXEC_PROPERTIES_IFACE "org.freedesktop.DBus.Properties"
#define SDEXEC_SERVICE_IFACE    "org.freedesktop.systemd1.Service"

#endif /* !_LIBSDEXEC_BUS_H */

// vi:ts=4 sw=4 expandtab
