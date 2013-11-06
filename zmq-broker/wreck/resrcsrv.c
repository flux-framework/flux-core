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

#include "route.h"
#include "cmbd.h"
#include "zmsg.h"
#include "log.h"
#include "util.h"
#include "plugin.h"

static void _store_hosts (plugin_ctx_t *p)
{
    char *key;
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    long pagesize = sysconf(_SC_PAGE_SIZE);
    long pages = sysconf(_SC_PHYS_PAGES);
    long memMB = pages * pagesize / 1024 / 1024;

    if ((asprintf (&key, "resrc.rank.%d.cores", flux_rank (p)) < 0) ||
	kvs_put_int64 (p, key, cores)) {
	err ("resrc: kvs_put_int64 %d %lu failed", flux_rank (p), cores);
    }
    free (key);
    if ((asprintf (&key, "resrc.rank.%d.mem", flux_rank (p)) < 0) ||
	kvs_put_int64 (p, key, memMB)) {
	err ("resrc: kvs_put_int64 %d %lu failed", flux_rank (p), memMB);
    }
    free (key);
    kvs_commit(p);
}

static void _init (plugin_ctx_t *p)
{
    _store_hosts(p);
}

struct plugin_struct resrcsrv = {
    .name      = "resrc",
    .initFn    = _init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
