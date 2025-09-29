/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _BROKER_PUBLISHER_H
#define _BROKER_PUBLISHER_H

#include "broker.h"

struct publisher *publisher_create (struct broker *ctx);
void publisher_destroy (struct publisher *pub);

#endif /* !_BROKER_PUBLISHER_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
