/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SHELL_RC_H
#define _SHELL_RC_H

int shell_rc (flux_shell_t *shell, const char *path);
int shell_rc_close (void);

#endif /* !_SHELL_RC_H */

/* vi: ts=4 sw=4 expandtab
 */

