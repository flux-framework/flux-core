/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SUBPROCESS_REMOTE_H
#define _SUBPROCESS_REMOTE_H

#include "subprocess.h"

int subprocess_remote_setup (flux_subprocess_t *p);

int remote_exec (flux_subprocess_t *p);

flux_future_t *remote_kill (flux_subprocess_t *p, int signum);

#endif /* !_SUBPROCESS_REMOTE_H */
