/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_DEBUGGED_H
#define _FLUX_CORE_DEBUGGED_H

#ifdef __cplusplus
extern "C" {
#endif

extern int MPIR_being_debugged;
extern void MPIR_Breakpoint (void);
extern int get_mpir_being_debugged (void);
extern void set_mpir_being_debugged (int v);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_DEBUGGED_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
