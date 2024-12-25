/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef SHELL_OUTPUT_TASK_H
#define SHELL_OUTPUT_TASK_H

#include <jansson.h>

#include <flux/shell.h>

#include "output/output.h"

struct shell_output;

struct task_output_list *task_output_list_create (struct shell_output *out);

void task_output_list_destroy (struct task_output_list *l);

int task_output_list_write (struct task_output_list *l, json_t *context);

/*  Return the file entry (if any) of `stream` for the local task `index`.
 */
struct file_entry *task_output_file_entry (struct task_output_list *l,
                                           char *stream,
                                           int index);
#endif /* !SHELL_OUTPUT_TASK_H */
