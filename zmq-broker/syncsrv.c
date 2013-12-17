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
#include "log.h"
#include "plugin.h"

#define MAX_SYNC_PERIOD_SEC 30*60.0

static int epoch = 0;
static bool disabled = false;

static int syncsrv_timeout (flux_t h)
{
    if (flux_event_send (h, NULL, "event.sched.trigger.%d", ++epoch) < 0) {
        err ("flux_event_send");
        return -1;
    }
    return 0;
}

static void set_config (const char *path, kvsdir_t dir, void *arg, int errnum)
{
    flux_t h = arg;
    double val;
    char *key;

    if (errnum > 0) {
        err ("sync: %s", path);
        goto invalid;
    }

    key = kvsdir_key_at (dir, "period-sec");
    if (kvs_get_double (h, key, &val) < 0) {
        err ("sync: %s", key);
        goto invalid;
    }
    if (val == NAN || val <= 0 || val > MAX_SYNC_PERIOD_SEC) {
        msg ("sync: %s must be > 0 and <= %.1f", key, MAX_SYNC_PERIOD_SEC);
        goto invalid;
    }
    free (key);

    if (disabled) {
        msg ("sync: %s values OK, synchronization resumed", path);
        disabled = false;
    }
    flux_timeout_set (h, (int)(val * 1000)); /* msec */
    return;
invalid:
    if (!disabled) {
        msg ("sync: %s values invalid, synchronization suspended", path);
        disabled = true;
        flux_timeout_set (h, 0);
    }
}

static int syncsrv_init (flux_t h, zhash_t *args)
{
    if (kvs_watch_dir (h, set_config, h, "conf.sync") < 0) {
        err ("kvs_watch_dir conf.sync");
        return -1;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    return 0;
}

const struct plugin_ops ops = {
    .init    = syncsrv_init,
    .timeout = syncsrv_timeout,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
