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
#include <sys/param.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/subprocess/subprocess.h"
#include "src/common/subprocess/command.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/base64.h"

#include "attr.h"
#include "exec2.h"

static void exec2_finalize (void *arg)
{
    flux_subprocess_server_t *s = arg;
    flux_subprocess_server_stop (s);
}

int exec2_terminate_subprocesses_by_uuid (flux_t *h, const char *id)
{
    flux_subprocess_server_t *s = flux_aux_get (h, "flux::exec2");

    if (!s) {
        flux_log (h, LOG_DEBUG, "no server_ctx found");
        return -1;
    }

    if (flux_subprocess_server_terminate_by_uuid (s, id) < 0) {
        flux_log_error (h, "flux_subprocess_server_terminate_by_uuid");
        return -1;
    }

    return 0;
}

int exec2_initialize (flux_t *h, uint32_t rank, attr_t *attrs)
{
    flux_subprocess_server_t *s = NULL;
    const char *local_uri;

    if (attr_get (attrs, "local-uri", &local_uri, NULL) < 0)
        goto cleanup;
    if (!(s = flux_subprocess_server_start (h, "cmb", local_uri, rank)))
        goto cleanup;
    flux_aux_set (h, "flux::exec2", s, exec2_finalize);
    return 0;
cleanup:
    flux_subprocess_server_stop (s);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
