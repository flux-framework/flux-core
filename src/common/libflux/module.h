/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_MODULE_H
#define _FLUX_CORE_MODULE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Module states, for embedding in keepalive messages (rfc 5)
 */
enum {
    FLUX_MODSTATE_INIT           = 0,
    FLUX_MODSTATE_RUNNING        = 1,
    FLUX_MODSTATE_FINALIZING     = 2,
    FLUX_MODSTATE_EXITED         = 3,
};

/* Mandatory symbols for modules
 */
#define MOD_NAME(x) const char *mod_name = x
typedef int (mod_main_f)(flux_t *h, int argc, char *argv[]);

/* Test and optionally clear module debug bit from within a module, as
 * described in RFC 5.  Return true if 'flag' bit is set.  If clear=true,
 * clear the bit after testing.  The flux-module(1) debug subcommand
 * manipulates these bits externally to set up test conditions.
 */
bool flux_module_debug_test (flux_t *h, int flag, bool clear);

/* Set module state to RUNNING.  This transition occurs automatically when the
 * reactor is entered, but this function can set the state to RUNNING early,
 * e.g. if flux module load must complete before the module enters the reactor.
 * Returns 0 on success, -1 on error with errno set.
 */
int flux_module_set_running (flux_t *h);

#ifdef __cplusplus
}
#endif

#endif /* !FLUX_CORE_MODULE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
