/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_FLUX_CMDHELP_H
#define HAVE_FLUX_CMDHELP_H

#include <stdio.h>

/*
 *  Read command information encoded in JSON from all files in
 *   [pattern], and print the result to file stream [fp] by
 *   "category" (e.g., "core", "sched", etc).
 */
void emit_command_help (const char *pattern, FILE *fp);

#endif /* !HAVE_FLUX_CMDHELP_H */
