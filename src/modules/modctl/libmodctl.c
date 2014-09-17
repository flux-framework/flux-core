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
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <ctype.h>
#include <stdarg.h>
#include <json.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"

int flux_modctl_rm (flux_t h, const char *name)
{
    JSON request = Jnew ();
    JSON response = NULL;
    int rc = -1;

    Jadd_str (request, "name", name);
    if ((response = flux_rpc (h, request, "modctl.rm"))) {
        errno = EPROTO;
        goto done;
    }
    if (errno != 0)
        goto done;
    rc = 0;
done:
    Jput (request);
    Jput (response);
    return rc;
}

int flux_modctl_ins (flux_t h, const char *name)
{
    JSON request = Jnew ();
    JSON response = NULL;
    int rc = -1;

    Jadd_str (request, "name", name);
    if ((response = flux_rpc (h, request, "modctl.ins"))) {
        errno = EPROTO;
        goto done;
    }
    if (errno != 0)
        goto done;
    rc = 0;
done:
    Jput (request);
    Jput (response);
    return rc;
}

int flux_modctl_update (flux_t h)
{
    JSON response = NULL;
    int rc = -1;

    if ((response = flux_rpc (h, NULL, "modctl.update"))) {
        errno = EPROTO;
        goto done;
    }
    if (errno != 0)
        goto done;
    rc = 0;
done:
    Jput (response);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
