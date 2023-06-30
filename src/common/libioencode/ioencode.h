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

/* encode io data and/or EOF into RFC24 data event object
 * - to set only EOF, set data to NULL and data_len to 0
 * - it is an error to provide no data and EOF = false
 * - returns RFC24 object on success, NULL on error with errno set
 * - returned object should be json_decref()'d after use
 */
json_t *ioencode (const char *stream,
                  const char *rank,
                  const char *data,
                  int len,
                  bool eof);

/* decode RFC24 data event object
 * - both data and EOF can be available
 * - if no data available, data set to NULL and len to 0
 * - data must be freed after return
 * - data can be NULL and len non-NULL to retrieve data length
 * - returns 0 on success, -1 on error with errno set
 */
int iodecode (json_t *o,
              const char **stream,
              const char **rank,
              char **data,
              int *len,
              bool *eof);

#endif /* !_IOENCODE_H */
