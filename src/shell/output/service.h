/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef SHELL_OUTPUT_SERVICE_H
#define SHELL_OUTPUT_SERVICE_H

#include "output/output.h"

struct shell_output;

struct output_service *output_service_create (struct shell_output *out,
                                              flux_plugin_t *p,
                                              int size);

void output_service_destroy (struct output_service *service);

/*  Redirect output handled by output service for 'stream' from the KVS
 *  to file described by 'fp'.
 */
void output_service_redirect (struct output_service *service,
                              const char *stream,
                              struct file_entry *fp);

#endif /* !SHELL_OUTPUT_SERVICE_H */
