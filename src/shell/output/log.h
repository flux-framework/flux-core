/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef SHELL_OUTPUT_LOG_H
#define SHELL_OUTPUT_LOG_H

#include <flux/core.h>
#include "output/output.h"

/* Initialize shell.log log output plugin callback
 */
void shell_output_log_init (flux_plugin_t *p, struct shell_output *out);

void shell_output_log_file (struct shell_output *out, json_t *context);

#endif /* !SHELL_OUTPUT_LOG_H */
