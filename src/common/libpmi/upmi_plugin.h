/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBPMI_UPMI_PLUGIN_H
#define _LIBPMI_UPMI_PLUGIN_H 1

#include "src/common/libflux/types.h"

/* Plugins must populate all of the following callbacks.
 */
struct upmi_plugin {
    const char *(*getname) (void);
    void *(*create) (struct upmi *upmi,
                     const char *path,
                     flux_error_t *error);
    void (*destroy) (void *data);
    int (*initialize) (void *data,
                       struct upmi_info *info,
                       flux_error_t *error);
    int (*finalize) (void *data, flux_error_t *error);
    int (*put) (void *data,
                const char *key,
                const char *value,
                flux_error_t *error);
    int (*get) (void *data,
                const char *key,
                int rank, // -1 if target rank is unknown
                char **value, // caller must free
                flux_error_t *error);
    int (*barrier) (void *data, flux_error_t *error);
};

void upmi_trace (struct upmi *upmi, const char *fmt, ...);
bool upmi_has_flag (struct upmi *upmi, int flag);

#endif /* !_LIBPMI_UPMI_PLUGIN_H */

// vi:ts=4 sw=4 expandtab
