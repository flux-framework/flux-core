/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <stdio.h>
#include <flux/core.h>
#include <flux/optparse.h>
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/environment.h"

#ifndef HAVE_CMD_BUILTIN_H
#define HAVE_CMD_BUILTIN_H 1

typedef int (*register_cmd_f) (optparse_t *p);

struct builtin_cmd {
    const char *   name;
    register_cmd_f reg_fn;
};

void usage (optparse_t *p);
void builtin_env_add_pythonpath (struct environment *env);
void builtin_env_fputs_quoted (FILE *fp, const char *s);
flux_t *builtin_get_flux_handle (optparse_t *p);

#endif /* !HAVE_CMD_BUILTIN_H */
