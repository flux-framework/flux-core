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

#include "zmq.h"
#include "route.h"
#include "cmb.h"
#include "cmbd.h"
#include "log.h"
#include "plugin.h"

#include "syncsrv.h"

static int epoch = 0;

static void _timeout (plugin_ctx_t *p)
{
    plugin_send_event (p, "event.sched.trigger.%d", ++epoch);
}

static void _init (plugin_ctx_t *p)
{
    double sync_period_sec;

    sync_period_sec = plugin_conf_get_double (p, "sync.period.sec");
    if (sync_period_sec <= 0 || sync_period_sec > 30*60)
        msg_exit ("sync: bad sync.period.sec value");
    plugin_timeout_set (p, (int)(sync_period_sec * 1000)); /* msec */
}

struct plugin_struct syncsrv = {
    .name      = "sync",
    .initFn    = _init,
    .timeoutFn = _timeout,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
