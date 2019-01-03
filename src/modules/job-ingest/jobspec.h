/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOBSPEC_WRAP_H
#define _FLUX_JOBSPEC_WRAP_H

#ifdef __cplusplus
extern "C" {
#endif

int jobspec_validate (const char *buf, int len,
                      char *errbuf, int errbufsz);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_JOBSPEC_WRAP_H */
