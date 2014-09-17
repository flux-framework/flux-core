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
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <json.h>
#include <czmq.h>
#include <signal.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"


int flux_panic (flux_t h, int rank, const char *msg)
{
    JSON request = Jnew ();
    int rc = -1;

    if (msg)
        Jadd_str (request, "msg", msg);
    if (flux_rank_request_send (h, rank, request, "cmb.panic") < 0)
        goto done;
    /* No reply */
    rc = 0;
done:
    Jput (request);
    return rc;
}

void flux_assfail (flux_t h, char *ass, char *file, int line)
{
    flux_log (h, LOG_CRIT, "assertion failure: %s:%d: %s", file, line, ass);
    sleep (5);
    if (raise (SIGABRT) < 0)
        exit (1);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
