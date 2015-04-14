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
#include <czmq.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"

#include "endpt.h"

endpt_t *endpt_create (const char *fmt, ...)
{
    endpt_t *ep = xzmalloc (sizeof (*ep));
    va_list ap;

    va_start (ap, fmt);
    ep->uri = xvasprintf (fmt, ap);
    va_end (ap);
    return ep;
}

void endpt_destroy (endpt_t *ep)
{
    free (ep->uri);
    free (ep);
}

int endpt_cc (zmsg_t *zmsg, endpt_t *ep)
{
    zmsg_t *cpy;

    if (!zmsg || !ep || !ep->zs)
        return 0;
    if (!(cpy = zmsg_dup (zmsg)))
        oom ();
    return zmsg_send (&cpy, ep->zs);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
