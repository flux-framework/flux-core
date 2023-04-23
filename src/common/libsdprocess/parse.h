/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _PARSE_H
#define _PARSE_H

#include <stdint.h>

/* string parsing functions */

/* parse strings in format "<double>%" */
int parse_percent (const char *s, double *percent);

/* parse strings in format "<unsigned long>[k,m,g,t]" */
int parse_unsigned (const char *s, uint64_t *num);

#endif /* !_PARSE_H */
