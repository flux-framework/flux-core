/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.  Additionally, Flux libraries may be redistributed
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation, either version 2 of the license,
 *  or (at your option) any later version.
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
# include "config.h"
#endif

#include <sys/types.h>
#include <wait.h>
#include <unistd.h>
#include <errno.h>

#include <czmq.h>

#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/fdwalk.h"

#include "subprocess.h"
#include "subprocess_private.h"
#include "util.h"

void init_pair_fds (int *fds)
{
    fds[0] = -1;
    fds[1] = -1;
}

void close_pair_fds (int *fds)
{
    if (!fds)
        return;
    if (fds[0] != -1)
        close (fds[0]);
    if (fds[1] != -1)
        close (fds[1]);
}

int cmd_option_bufsize (flux_subprocess_t *p, const char *name)
{
    char *var;
    const char *val;
    int rv = -1;

    if (asprintf (&var, "%s_BUFSIZE", name) < 0) {
        log_err ("asprintf");
        goto cleanup;
    }

    if ((val = flux_cmd_getopt (p->cmd, var))) {
        char *endptr;
        errno = 0;
        rv = strtol (val, &endptr, 10);
        if (errno
            || endptr[0] != '\0'
            || rv <= 0) {
            rv = -1;
            errno = EINVAL;
            goto cleanup;
        }
    }
    else
        rv = SUBPROCESS_DEFAULT_BUFSIZE;

cleanup:
    free (var);
    return rv;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
