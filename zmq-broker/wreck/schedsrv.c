/* schedsrv.c - scheduling service */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <czmq.h>
#include <json/json.h>

#include "zmsg.h"
#include "log.h"
#include "util.h"
#include "plugin.h"


static void watchid (const char *key, kvsdir_t dir, void *arg, int errnum)
{
    flux_t p = (flux_t)arg;
    const char *js;
    json_object *o;

    if (errnum > 0) {
        flux_log (p, LOG_ERR, "%s: %s\n", key, strerror (errnum));
    } else if (kvs_get (p, key, &o) < 0) {
        flux_log (p, LOG_ERR, "%s: %s\n", key, strerror (errno));
    } else {
        js = json_object_to_json_string_ext (o, JSON_C_TO_STRING_PLAIN);
        flux_log (p, LOG_INFO, "found %s = %s", key, js);
        json_object_put (o);
    }
}

static int _sched_main (flux_t p, zhash_t *args)
{
    kvsdir_t dir = NULL;
    int rc = 0;

    flux_log_set_facility (p, "sched");
    flux_log (p, LOG_INFO, "sched plugin starting");

    while (((rc = kvs_watch_once_dir (p, &dir, "lwj")) == 0) || (rc < 0)) {
        flux_log (p, LOG_INFO, "watch_once %d, %d, %s",
                    rc, errno, kvsdir_key(dir));
        if (rc < 0) {
            flux_log (p, LOG_ERR, "watch_once_dir: %d, %s",
                        rc, strerror (errno));
            if (dir) {
                kvsdir_destroy (dir);
                dir = NULL;
            }
        } else
            break;
    }

    flux_log (p, LOG_INFO, "sched watching for next-id");

    while (((rc = kvs_watch_int64 (p, "lwj.next-id", (KVSSetInt64F*)watchid, p))
            == 0) || (rc < 0)) {
        flux_log (p, LOG_INFO, "proceeding watch_dir %d", rc);
        if (rc < 0) {
            flux_log (p, LOG_ERR, "watch lwj.next-id: %s\n", strerror (errno));
        } else
            break;
    }

    flux_log (p, LOG_INFO, "sched plugin initialized");

    if (flux_reactor_start (p) < 0) {
        flux_log (p, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    return 0;
}

const struct plugin_ops ops = {
    .main = _sched_main,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
