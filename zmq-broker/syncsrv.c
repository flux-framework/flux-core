/* syncsrv.c - generate scheduling trigger */

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
#include <ctype.h>
#include <zmq.h>
#include <czmq.h>
#include <json/json.h>

#include "zmsg.h"
#include "route.h"
#include "cmbd.h"
#include "log.h"
#include "plugin.h"

#define MAX_SYNC_PERIOD_SEC 30*60.0

static int epoch = 0;
static bool disabled = false;

static void _timeout (plugin_ctx_t *p)
{
    plugin_send_event (p, "event.sched.trigger.%d", ++epoch);
}

static void set_config (const char *key, kvsdir_t dir, void *arg, int errnum)
{
    plugin_ctx_t *p = arg;
    double val;

    if (errnum > 0) {
        err ("sync: %s", key);
        goto invalid;
    }
    if (kvsdir_get_double (dir, "period-sec", &val) < 0) {
        err ("sync: %s.period-sec", key);
        goto invalid;
    }
    if (val == NAN || val <= 0 || val > MAX_SYNC_PERIOD_SEC) {
        msg ("sync: %s.period-sec must be > 0 and <= %.1f", key,
             MAX_SYNC_PERIOD_SEC);
        goto invalid;
    }
    if (disabled) {
        msg ("sync: %s values OK, synchronization resumed", key);
        disabled = false;
    }
    plugin_timeout_set (p, (int)(val * 1000)); /* msec */
    return;
invalid:
    if (!disabled) {
        msg ("sync: %s values invalid, synchronization suspended", key);
        disabled = true;
        plugin_timeout_clear (p);
    }
}

static void _init (plugin_ctx_t *p)
{
    if (kvs_watch_dir (p, set_config, p, "conf.sync") < 0)
        err_exit ("kvs_watch_dir conf.sync");
}

struct plugin_struct syncsrv = {
    .name      = "sync",
    .initFn    = _init,
    .timeoutFn = _timeout,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
