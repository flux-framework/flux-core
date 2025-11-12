/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBPMI_UPMI_H
#define _LIBPMI_UPMI_H 1

#include <sys/types.h>
#include <stdarg.h>
#include <stdbool.h>
#include <jansson.h>

#include "src/common/libflux/types.h"

enum {
    UPMI_TRACE = 1,         // call the trace callback for each operation
    UPMI_LIBPMI_NOFLUX = 2, // libpmi should fail if Flux libflux.so is found
    UPMI_LIBPMI2_CRAY = 4,  // force cray libpmi2 workarounds for testing
};

struct upmi_info {
    int rank;
    int size;
    const char *name;
    json_t *dict; // may be NULL and is invalidated by the next upmi API call
};

typedef void (*upmi_trace_f)(void *arg, const char *text);

struct upmi *upmi_create (const char *spec,
                          int flags,
                          upmi_trace_f cb,
                          void *arg,
                          flux_error_t *error);
struct upmi *upmi_create_ex (const char *spec,
                             int flags,
                             json_t *args,
                             upmi_trace_f cb,
                             void *arg,
                             flux_error_t *error);
void upmi_destroy (struct upmi *upmi);
const char *upmi_describe (struct upmi *upmi);

int upmi_initialize (struct upmi *upmi,
                     struct upmi_info *info,
                     flux_error_t *error);
int upmi_finalize (struct upmi *upmi, flux_error_t *error);

int upmi_put (struct upmi *upmi,
              const char *key,
              const char *value,
              flux_error_t *error);
int upmi_get (struct upmi *upmi,
              const char *key,
              int rank, // -1 if target rank is unknown
              char **value, // caller must free
              flux_error_t *error);
int upmi_barrier (struct upmi *upmi,
                  flux_error_t *error);
int upmi_abort (struct upmi *upmi, const char *msg, flux_error_t *error);

#endif /* !_LIBPMI_UPMI_H */

// vi:ts=4 sw=4 expandtab
