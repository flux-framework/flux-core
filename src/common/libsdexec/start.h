/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBSDEXEC_START_H
#define _LIBSDEXEC_START_H

#include <flux/core.h>

/* Call systemd StartTransientUnit with parameters from libsubprocess style
 * command object. The SDEXEC_NAME command option must be set to the unit
 * name (with .service suffx).
 *
 * See https://www.freedesktop.org/wiki/Software/systemd/dbus/
 * and systemd.service(5) for more info on mode and type parameters.
 * mode may be one of:
     replace, fail, isolate, ignore-dependencies, ignore-requirements
 *
 * stdin_fd, stdout_fd, and stderr_fd are file descriptors to be duped and
 * passed to the new unit.  The dup should be complete on first fulfillment
 * of the future and local copies can be closed at that time.  Set to -1 to
 * indicate that systemd should manage a particular stdio stream.
 *
 * Service unit properties may be set with command options prefixed with
 * SDEXEC_PROP_.  The following unit properties are explicitly parsed and
 * converted to their native types:
 *
 * MemoryHigh, MemoryMax, MemoryLow, MemoryMin
 *   Value may be "infinity", a percentage of physical memory ("98%"),
 *   or a quantity with optional base 1024 K, M, G, or T suffix ("8g").
 *   See also: systemd.resource-control(5).
 *
 * AllowedCPUs
 *   Restrict execution to specific CPUs. Value is a Flux idset representing
 *   a list of CPU indices.
 *   See also: systemd.resource-control(5).
 *
 * Other service unit properties are assumed to be of type string and are
 * set without conversion.  For example, Description may be set to a string
 * that is shown in list-units output.
 */
flux_future_t *sdexec_start_transient_unit (flux_t *h,
                                            uint32_t rank,
                                            const char *mode,
                                            json_t *cmd,
                                            int stdin_fd,
                                            int stdout_fd,
                                            int stderr_fd,
                                            flux_error_t *error);

int sdexec_start_transient_unit_get (flux_future_t *f, const char **job);

#endif /* !_LIBSDEXEC_START_H */

// vi:ts=4 sw=4 expandtab
