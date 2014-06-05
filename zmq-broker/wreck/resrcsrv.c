/* resrcsrv.c - resource store */

#define _GNU_SOURCE
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
#include <json/json.h>

#include "zmsg.h"
#include "log.h"
#include "util.h"
#include "plugin.h"

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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
