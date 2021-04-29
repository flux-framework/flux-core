/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _CZMQ_CONTAINERS_H
#define _CZMQ_CONTAINERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "czmq_rename.h"

typedef struct _zhashx_t zhashx_t;
typedef struct _zlistx_t zlistx_t;
typedef struct _zhash_t zhash_t;
typedef struct _zlist_t zlist_t;

#ifndef CZMQ_EXPORT
#define CZMQ_EXPORT
#endif

#include "zhashx.h"
#include "zlistx.h"
#include "zhash.h"
#include "zlist.h"

#ifdef __cplusplus
}
#endif

#endif
