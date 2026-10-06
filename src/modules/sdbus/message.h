/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SDBUS_MESSAGE_H
#define _SDBUS_MESSAGE_H

#include <systemd/sd-bus.h>
#include <jansson.h>

const char *sdmsg_typestr (sd_bus_message *m);

/* Append the values in JSON array 'params' to message 'm', where 'sig'
 * is the D-Bus signature of the values.  The JSON encoding of each value
 * is described in RFC 52.  Return 0 on success, or -errno on failure.
 */
int sdmsg_write (sd_bus_message *m, const char *sig, json_t *params);

/* Read the remaining values at the current level of message 'm' and
 * append them to JSON array 'params'.  D-Bus messages are self-describing,
 * so no signature is needed.  Return 0 on success, or -errno on failure.
 */
int sdmsg_read (sd_bus_message *m, json_t *params);

#endif /* !_SDBUS_MESSAGE_H */

// vi:ts=4 sw=4 expandtab
