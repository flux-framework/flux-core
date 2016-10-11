/*****************************************************************************\
 *  Copyright (c) 2015 Lawrence Livermore National Security, LLC.  Produced at
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

#ifndef _FLUX_CORE_JSTATCTRL_DEPRECATED_H
#define _FLUX_CORE_JSTATCTRL_DEPRECATED_H 1

#include "jstatctl.h"
#include "src/common/libjson-c/json.h"

typedef int (*jsc_handler_obj_f)(json_object *base_jcb, void *arg, int errnum);

int jsc_notify_status_obj (flux_t *h, jsc_handler_obj_f callback, void *d)
                           __attribute__ ((deprecated));
int jsc_query_jcb_obj (flux_t *h, int64_t jobid, const char *key,
                       json_object **jcb)
                       __attribute__ ((deprecated));
int jsc_update_jcb_obj (flux_t *h, int64_t jobid, const char *key,
                        json_object *jcb)
                       __attribute__ ((deprecated));

#endif /*! _FLUX_CORE_JSTATCTRL_DEPRECATED_H */

/*
 * vi: ts=4 sw=4 expandtab
 */
