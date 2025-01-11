/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_SHELL_OUTPUT_CONFIG_H
#define HAVE_SHELL_OUTPUT_CONFIG_H

#include <flux/shell.h>

#include "output/filehash.h"

enum {
    FLUX_OUTPUT_TYPE_KVS = 0,
    FLUX_OUTPUT_TYPE_FILE = 1,
};

struct output_stream {
    int type;
    const char *buffer_type;
    const char *template;
    const char *mode;
    bool label;
    bool per_shell;
};

struct output_config {
    struct output_stream out;
    struct output_stream err;
};

struct output_config *output_config_create (flux_shell_t *shell);

void output_config_destroy (struct output_config *conf);

#endif /* !HAVE_SHELL_OUTPUT_CONFIG_H */
