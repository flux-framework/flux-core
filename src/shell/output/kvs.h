/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/*  Output KVS destination functions
 */
#ifndef SHELL_OUTPUT_KVS_H
#define SHELL_OUTPUT_KVS_H

#include <jansson.h>

#include <flux/core.h>
#include <flux/shell.h>

struct kvs_output *kvs_output_create (flux_shell_t *shell);

void kvs_output_destroy (struct kvs_output *kvs);

void kvs_output_flush (struct kvs_output *kvs);

void kvs_output_close (struct kvs_output *kvs);

int kvs_output_write_entry (struct kvs_output *kvs,
                            const char *type,
                            json_t *context);

int kvs_output_redirect (struct kvs_output *kvs,
                         const char *stream,
                         const char *path);

void kvs_output_reconnect (struct kvs_output *kvs);

#endif /* !SHELL_OUTPUT_KVS_H */

