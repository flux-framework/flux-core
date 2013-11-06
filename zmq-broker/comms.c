/* comms.c - group rpc, etc */ 
/* Group RPC event format:
 *    mrpc.<uuid>.<nodelist>.<kvs_version>.[<plugin>[.<method>...]]
 * Group RPC kvs directory:
 *    mrpc.<uuid>
 */

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
#include <ctype.h>
#include <stdarg.h>
#include <json/json.h>
#include <czmq.h>

#include "hostlist.h"
#include "log.h"
#include "zmsg.h"
#include "util.h"
#include "kvs.h"

#include "comms.h"

struct flux_rpc_struct {
    char *path;
    hostlist_t nodelist;
    int nprocs;
    void *h;
    kvsdir_t dir;
    kvsitr_t itr;
};

void flux_rpc_create (void *h, const char *nodelist, flux_rpc_t *fp)
{
    flux_rpc_t f = xzmalloc (sizeof (*f));

    if (asprintf (&f->path, "mrpc.%s", uuid_generate_str ()) < 0)
        oom ();
    f->nodelist = hostlist_create (nodelist);
    f->h = h;

    *fp = f;
}

void flux_rpc_destroy (flux_rpc_t f)
{
    if (kvs_unlink (f->h, f->path) < 0)
        err ("kvs_unlink %s", f->path);
    if (kvs_commit (f->h) < 0)
        err ("kvs_commit");
    if (f->dir)
        kvsdir_destroy (f->dir);
    if (f->itr)
        kvsitr_destroy (f->itr);
    free (f->path);
    free (f);
}

void flux_rpc_put_inarg (flux_rpc_t f, json_object *val)
{
    char *key;

    if (asprintf (&key, "%s.in", f->path) < 0)
        oom ();
    kvs_put (f->h, key, val);
    free (key);
}

int flux_rpc_get_inarg (flux_rpc_t f, json_object **valp)
{
    char *key;
    int rc = -1;

    if (asprintf (&key, "%s.in", f->path) < 0)
        oom ();
    if (kvs_get (f->h, key, valp) < 0)
        goto done;
    rc = 0;
done:
    free (key);
    return rc;
}

void flux_rpc_put_outarg (flux_rpc_t f, char *node, json_object *val)
{
    char *key;

    if (asprintf (&key, "%s.out-%s", f->path, node) < 0)
        oom ();
    kvs_put (f->h, key, val);
    free (key);
}

int flux_rpc_get_outarg (flux_rpc_t f, char *node, json_object **valp)
{
    char *key;
    int rc = -1;

    if (asprintf (&key, "%s.out-%s", f->path, node) < 0)
        oom ();
    if (kvs_get (f->h, key, valp) < 0)
        goto done;
    rc = 0;
done:
    free (key);
    return rc;
}

const char *flux_rpc_next_outarg (flux_rpc_t f)
{
    const char *next = NULL;

    if (!f->dir) {
        if (kvs_get_dir (f->h, &f->dir, "%s", f->path) < 0)
            goto done;
        f->itr = kvsitr_create (f->dir);
    }
    while ((next = kvsitr_next (f->itr))) {
        if (!strncmp (next, "out-", 4)) {
            next += 4;
            break;
        }
    }
done:
    return next;
}

void flux_rpc_rewind_outarg (flux_rpc_t f)
{
    if (f->itr)
        kvsitr_rewind (f->itr);
}

static char *nodelist_string (hostlist_t hl)
{
    char *s;
    int len = 64;

    s = xzmalloc (len);
    while (hostlist_ranged_string (hl, len, s) < 0) {
        if (!(s = realloc (s, len += 64)))
            oom ();
    }
    return s;
}

int flux_mrpc (flux_rpc_t f, const char *fmt, ...)
{
    int rc = -1;
    int version;
    char *tag = NULL, *nodelist = NULL;
    va_list ap;
    int nprocs;

    va_start (ap, fmt);
    if (vasprintf (&tag, fmt, ap) < 0)
        oom ();
    va_end (ap);

    hostlist_uniq (f->nodelist);
    nprocs = hostlist_count (f->nodelist);
    nodelist = nodelist_string (f->nodelist);

    if (kvs_commit (f->h) < 0)
        goto done;
    if (kvs_get_version (f->h, &version) < 0)
        goto done;
    if (flux_event_send (f->h, "%s.%s.%d.%s",   
                         f->path, nodelist, version, tag) < 0)
        goto done;
    if (kvs_fence (f->h, f->path, nprocs) < 0)
        goto done;
    rc = 0;
done:
    if (tag)
        free (tag);
    if (nodelist)
        free (nodelist);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
