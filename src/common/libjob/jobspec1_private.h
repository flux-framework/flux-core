/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _JOBSPEC1_PRIVATE_H
#define _JOBSPEC1_PRIVATE_H

json_t *jobspec1_get_json (flux_jobspec1_t *jobspec);
flux_jobspec1_t *jobspec1_from_json (json_t *obj);

#endif // !_JOBSPEC1_PRIVATE_H

// vi:ts=4 sw=4 expandtab
