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

/* Put one value (or container) specified by 'fmt' from json object 'o' to
 * current cursor position of message 'm'.  Return 0 on success, or -errno
 * on failure.
 */
int sdmsg_put (sd_bus_message *m, const char *fmt, json_t *o);

/* Put list of values specified by 'fmt' from json array 'o' to current cursor
 * position of message 'm'.  Return 0 on success or -errno on failure.
 */
int sdmsg_write (sd_bus_message *m, const char *fmt, json_t *o);

/* Get one value (or container) specified by 'fmt' from message 'm' at the
 * current cursor position and return in a new json object assigned to 'op'.
 * Return 1 on success, or -errno on failure.
 */
int sdmsg_get (sd_bus_message *m, const char *fmt, json_t **op);

/* Get list of values specified by 'fmt' from message 'm' at the current cursor
 * position and append them to the json array 'o'.
 * Return 1 on success, or -errno on falure.
 */
int sdmsg_read (sd_bus_message *m, const char *fmt, json_t *o);

#endif /* !_SDBUS_MESSAGE_H */

// vi:ts=4 sw=4 expandtab
