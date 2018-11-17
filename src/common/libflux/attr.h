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

#ifndef _FLUX_CORE_ATTR_H
#define _FLUX_CORE_ATTR_H

#include "handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Flags can only be set by the broker.
 */
enum {
    FLUX_ATTRFLAG_IMMUTABLE = 1,    /* attribute is cacheable */
    FLUX_ATTRFLAG_READONLY = 2,     /* attribute cannot be written */
                                    /*   but may change on broker */
    FLUX_ATTRFLAG_ACTIVE = 4,       /* attribute has get and/or set callbacks */
};

const char *flux_attr_get (flux_t *h, const char *name);

int flux_attr_set (flux_t *h, const char *name, const char *val);


/* hotwire flux_attr_get()'s cache for testing */
int flux_attr_set_cacheonly (flux_t *h, const char *name, const char *val);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_ATTR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
