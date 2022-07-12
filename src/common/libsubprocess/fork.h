/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SUBPROCESS_FORK_H
#define _SUBPROCESS_FORK_H

#include "subprocess.h"

int create_process_fork (flux_subprocess_t *p);

#endif /* !_SUBPROCESS_FORK_H */
