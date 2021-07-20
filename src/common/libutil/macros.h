/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef LIBUTIL_MACROS_H
#define LIBUTIL_MACROS_H 1

#define REAL_STRINGIFY(X) #X
#define STRINGIFY(X) REAL_STRINGIFY (X)
#define SIZEOF_FIELD(type, field) sizeof (((type *)0)->field)

#endif
