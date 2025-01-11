/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/*  Output service client functions
 */
#ifndef SHELL_OUTPUT_CLIENT_H
#define SHELL_OUTPUT_CLIENT_H

#include <jansson.h>

#include <flux/core.h>
#include <flux/shell.h>

struct output_client *output_client_create (flux_shell_t *shell);

void output_client_destroy (struct output_client *client);

int output_client_send (struct output_client *client,
                        const char *type,
                        json_t *context);

#endif /* !SHELL_OUTPUT_CLIENT_H */

