/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SDBUS_INTERFACE_H
#define _SDBUS_INTERFACE_H

#include <systemd/sd-bus.h>
#include <jansson.h>
#include <flux/core.h>

sd_bus_message *interface_request_fromjson (sd_bus *bus,
                                            json_t *req,
                                            flux_error_t *error);

json_t *interface_reply_tojson (sd_bus_message *m,
                                const char *interface,
                                const char *member,
                                flux_error_t *error);

json_t *interface_signal_tojson (sd_bus_message *m, flux_error_t *error);

#endif /* !_SDBUS_INTERFACE_H */

// vi:ts=4 sw=4 expandtab
