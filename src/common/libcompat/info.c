/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/param.h>

#include "src/common/libflux/handle.h"
#include "src/common/libflux/info.h"
#include "info.h"

int flux_rank (flux_t *h)
{
    uint32_t rank;
    if (flux_get_rank (h, &rank) < 0)
        return -1;
    if (rank >= INT_MAX) {
        errno = ERANGE;
        return -1;
    }
    return rank;
}

int flux_size (flux_t *h)
{
    uint32_t size;
    if (flux_get_size (h, &size) < 0)
        return -1;
    if (size >= INT_MAX) {
        errno = ERANGE;
        return -1;
    }
    return size;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
