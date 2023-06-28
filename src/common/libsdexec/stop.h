/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBSDEXEC_STOP_H
#define _LIBSDEXEC_STOP_H

#include <flux/core.h>

/* See https://www.freedesktop.org/wiki/Software/systemd/dbus/
 * for more info on mode parameter.
 * mode maybe one of: replace, fail, ignore-dependencies, ignore-requirements.
 */
flux_future_t *sdexec_stop_unit (flux_t *h,
                                 uint32_t rank,
                                 const char *name,
                                 const char *mode);

flux_future_t *sdexec_reset_failed_unit (flux_t *h,
                                         uint32_t rank,
                                         const char *name);

flux_future_t *sdexec_kill_unit (flux_t *h,
                                 uint32_t rank,
                                 const char *name,
                                 const char *who, // main/control/all
                                 int signum);

#endif /* !_LIBSDEXEC_STOP_H */

// vi:ts=4 sw=4 expandtab
