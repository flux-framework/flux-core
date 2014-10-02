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

/* resrcsrv.c - resource store */

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
#include <sys/time.h>
#include <unistd.h>
#include <ctype.h>
#include <zmq.h>
#include <czmq.h>

#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/jsonutil.h"
#include "src/modules/kvs/kvs.h"

static void _store_hosts (flux_t h)
{
    char *key;
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    long pagesize = sysconf(_SC_PAGE_SIZE);
    long pages = sysconf(_SC_PHYS_PAGES);
    long memMB = pages * pagesize / 1024 / 1024;

    if ((asprintf (&key, "resrc.rank.%d.cores", flux_rank (h)) < 0) ||
	kvs_put_int64 (h, key, cores)) {
	err ("resrc: kvs_put_int64 %d %lu failed", flux_rank (h), cores);
    }
    free (key);
    if ((asprintf (&key, "resrc.rank.%d.alloc.cores", flux_rank (h)) < 0) ||
	kvs_put_int64 (h, key, 0)) {
	err ("resrc: kvs_put_int64 %d %d failed", flux_rank (h), 0);
    }
    free (key);
    if ((asprintf (&key, "resrc.rank.%d.mem", flux_rank (h)) < 0) ||
	kvs_put_int64 (h, key, memMB)) {
	err ("resrc: kvs_put_int64 %d %lu failed", flux_rank (h), memMB);
    }
    free (key);
    kvs_commit(h);
}

int mod_main (flux_t h, zhash_t *args)
{
    _store_hosts(h);
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    return 0;
}

MOD_NAME ("resrc");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
