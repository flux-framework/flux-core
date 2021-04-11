/*  =========================================================================
    Copyright (c) the Contributors as noted in the AUTHORS file.
    This file is part of CZMQ, the high-level C binding for 0MQ:
    http://czmq.zeromq.org.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================
*/

/* To avoid copying in an excess amount of code from czmq, the
 * following have been manually cut and pasted in
 */

#ifndef __CZMQ_INTERNAL__
#define __CZMQ_INTERNAL__

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>

#ifdef NDEBUG
  #undef NDEBUG
  #include <assert.h>
  #define NDEBUG
#else
  #include <assert.h>
#endif

#define freen(x) do {free(x); x = NULL;} while(0)

#ifndef CZMQ_EXPORT
#define CZMQ_EXPORT
#endif

typedef struct _zhashx_t fzhashx_t;

#endif

