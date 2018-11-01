/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#ifndef _FLUX_CORE_REDUCE_H
#define _FLUX_CORE_REDUCE_H

#include "handle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct flux_reduce_struct flux_reduce_t;

struct flux_reduce_ops {
    flux_free_f destroy;
    void   (*reduce)(flux_reduce_t *r, int batchnum, void *arg);
    void   (*sink)(flux_reduce_t *r, int batchnum, void *arg);
    void   (*forward)(flux_reduce_t *r, int batchnum, void *arg);
    int    (*itemweight)(void *item);
};

enum {
    FLUX_REDUCE_TIMEDFLUSH = 1,
    FLUX_REDUCE_HWMFLUSH = 2,
};

enum {
    FLUX_REDUCE_OPT_TIMEOUT = 1,
    FLUX_REDUCE_OPT_HWM = 2,
    FLUX_REDUCE_OPT_COUNT = 3,
    FLUX_REDUCE_OPT_WCOUNT = 4,
};

flux_reduce_t *flux_reduce_create (flux_t *h, struct flux_reduce_ops ops,
                                   double timeout, void *arg, int flags);

void flux_reduce_destroy (flux_reduce_t *r);

int flux_reduce_append (flux_reduce_t *r, void *item, int batchnum);

void *flux_reduce_pop (flux_reduce_t *r);

int flux_reduce_push (flux_reduce_t *r, void *item);

int flux_reduce_opt_get (flux_reduce_t *r, int option, void *val, size_t size);

int flux_reduce_opt_set (flux_reduce_t *r, int option, void *val, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* _FLUX_CORE_REDUCE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
