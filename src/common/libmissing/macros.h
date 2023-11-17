/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef LIBMISSING_MACROS_H
#define LIBMISSING_MACROS_H 1

// borrowed from glibc-2.38.9000-255-g323f367cc4
#ifndef __W_EXITCODE
#define __W_EXITCODE(ret, sig)  ((ret) << 8 | (sig))
#endif

#endif
