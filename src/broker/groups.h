/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _BROKER_GROUPS_H
#define _BROKER_GROUPS_H

#include <flux/core.h>
#include "src/common/libidset/idset.h"

#include "broker.h"

struct groups *groups_create (struct broker *ctx);
void groups_destroy (struct groups *g);

#endif // !_BROKER_GROUPS_H

// vi:ts=4 sw=4 expandtab
