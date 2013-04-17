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
#include "cmbd.h"
#include "syncsrv.h"

static void _poll (plugin_ctx_t *p)
{
    for (;;) {
        usleep (p->conf->syncperiod_msec * 1000);
        cmb_msg_send (p->zs_out_event, "event.sched.trigger");
    }
}

struct plugin_struct syncsrv = {
    .pollFn = _poll,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
