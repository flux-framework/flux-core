/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _JOB_INGEST_UTIL_H_
#define _JOB_INGEST_UTIL_H_

#include <jansson.h>

char *util_join_arguments (json_t *o);

#endif /* !_JOB_INGEST_UTIL_H */

// vi:ts=4 sw=4 expandtab
