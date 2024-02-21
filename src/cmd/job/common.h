/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef FLUX_JOB_COMMON_H
#define FLUX_JOB_COMMON_H 1

#include <flux/optparse.h>

flux_jobid_t parse_jobid (const char *s);
unsigned long long parse_arg_unsigned (const char *s, const char *name);
char *parse_arg_message (char **argv, const char *name);
int parse_arg_states (optparse_t *p, const char *optname);
uint32_t parse_arg_userid (optparse_t *p, const char *optname);
char *trim_string (char *s);

#endif /* !FLUX_JOB_H */
