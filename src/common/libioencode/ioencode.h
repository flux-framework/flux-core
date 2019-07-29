/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _IOENCODE_H
#define _IOENCODE_H

#include <stdbool.h>
#include <jansson.h>

/* encode io data and/or EOF into json_t object
 * - to set only EOF, set data to NULL and data_len to 0
 * - it is an error to provide no data and EOF = false
 * - returned object should be json_decref()'d after use
 */
json_t *ioencode (const char *stream,
                  int rank,
                  const char *data,
                  int len,
                  bool eof);

/* decode json_t io object
 * - both data and EOF can be available
 * - if no data available, data set to NULL and len to 0
 * - data must be freed after return
 */
int iodecode (json_t *o,
              const char **stream,
              int *rank,
              char **data,
              int *len,
              bool *eof);

#endif /* !_IOENCODE_H */
