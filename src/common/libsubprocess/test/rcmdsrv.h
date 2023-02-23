/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef _SUBPROCESS_TEST_RCMDSRV_H
#define _SUBPROCESS_TEST_RCMDSRV_H

/* Start subprocess server.  Returns one end of back-to-back flux_t test
 * handle.  Call test_server_stop (h) when done to join with server thread.
 */
flux_t *rcmdsrv_create (void);

/* llog-compatible logger
 */
void tap_logger (void *arg,
                 const char *file,
                 int line,
                 const char *func,
                 const char *subsys,
                 int level,
                 const char *fmt,
                 va_list ap);

#endif // !_SUBPROCESS_TEST_RCMDSRV_H

// vi: ts=4 sw=4 expandtab
