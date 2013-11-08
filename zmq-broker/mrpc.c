/* mrpc.c - group rpc implementatino */ 

/* Group RPC event format:
 *    tag:  mrpc.<plugin>.<method>[.<method>]...
 *    JSON: path="mrpc.<uuid>"
 *          dest="nodelist"
 *          vers=N
 *          sender=N
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

#include "flux.h"

struct flux_mrpc_struct {
    char *path;
    hostlist_t nodelist;
    int nprocs;
    int sender;
    int kvs_version;
    void *h;
    kvsdir_t dir;
    kvsitr_t itr;
};

flux_mrpc_t flux_mrpc_create (void *h, const char *nodelist)
{
    flux_mrpc_t f = xzmalloc (sizeof (*f));

    if (!(f->nodelist = hostlist_create (nodelist))) {
        errno = EINVAL;
        goto error;
    }
    if (asprintf (&f->path, "mrpc.%s", uuid_generate_str ()) < 0)
        oom ();
    f->h = h;
    f->sender = flux_rank (h);

    return f;
error:
    flux_mrpc_destroy (f);
    return NULL;
}

void flux_mrpc_destroy (flux_mrpc_t f)
{
    if (f->path) {
        if (f->sender == flux_rank (f->h)) {
            if (kvs_unlink (f->h, f->path) < 0)
                err ("kvs_unlink %s", f->path);
            if (kvs_commit (f->h) < 0)
                err ("kvs_commit");
        }
        free (f->path);
    }
    if (f->dir)
        kvsdir_destroy (f->dir);
    if (f->itr)
        kvsitr_destroy (f->itr);
    free (f);
}

void flux_mrpc_put_inarg (flux_mrpc_t f, json_object *val)
{
    char *key;

    if (asprintf (&key, "%s.in", f->path) < 0)
        oom ();
    kvs_put (f->h, key, val);
    free (key);
}

int flux_mrpc_get_inarg (flux_mrpc_t f, json_object **valp)
{
    char *key;
    int rc = -1;

    if (asprintf (&key, "%s.in", f->path) < 0)
        oom ();
    msg ("Retrieving key %s.in", f->path);
    if (kvs_get (f->h, key, valp) < 0)
        goto done;
    rc = 0;
done:
    free (key);
    return rc;
}

void flux_mrpc_put_outarg (flux_mrpc_t f, json_object *val)
{
    char *key;

    if (asprintf (&key, "%s.out-%d", f->path, flux_rank (f->h)) < 0)
        oom ();
    kvs_put (f->h, key, val);
    free (key);
}

int flux_mrpc_get_outarg (flux_mrpc_t f, int nodeid, json_object **valp)
{
    char *key;
    int rc = -1;

    if (asprintf (&key, "%s.out-%d", f->path, nodeid) < 0)
        oom ();
    if (kvs_get (f->h, key, valp) < 0)
        goto done;
    rc = 0;
done:
    free (key);
    return rc;
}

int flux_mrpc_next_outarg (flux_mrpc_t f)
{
    const char *next = NULL;

    if (!f->dir) {
        if (kvs_get_dir (f->h, &f->dir, "%s", f->path) < 0)
            return -1;
        f->itr = kvsitr_create (f->dir);
    }
    while ((next = kvsitr_next (f->itr))) {
        if (!strncmp (next, "out-", 4))
            return strtoul (next + 4, NULL, 10);
    }
    return -1;
}

void flux_mrpc_rewind_outarg (flux_mrpc_t f)
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

static bool nodelist_member (hostlist_t hl, int node)
{
    char s[16];
    snprintf (s, sizeof (s), "%d", node);
    if (hostlist_find (hl, s) < 0)
        return false;
    return true;
}

int flux_mrpc (flux_mrpc_t f, const char *fmt, ...)
{
    int rc = -1;
    char *tag = NULL, *nodelist = NULL;
    va_list ap;
    json_object *request = util_json_object_new_object ();

    va_start (ap, fmt);
    if (vasprintf (&tag, fmt, ap) < 0)
        oom ();
    va_end (ap);

    hostlist_uniq (f->nodelist);
    f->nprocs = hostlist_count (f->nodelist);
    nodelist = nodelist_string (f->nodelist);
    util_json_object_add_string (request, "dest", nodelist);

    if (kvs_commit (f->h) < 0)
        goto done;
    if (kvs_get_version (f->h, &f->kvs_version) < 0)
        goto done;
    util_json_object_add_int (request, "vers", f->kvs_version);
    util_json_object_add_int (request, "sender", f->sender);

    util_json_object_add_string (request, "path", f->path);
    if (flux_event_send (f->h, request, "mrpc.%s", tag) < 0)
        goto done;
    if (kvs_fence (f->h, f->path, f->nprocs + 1) < 0)
        goto done;
    rc = 0;
done:
    if (tag)
        free (tag);
    if (nodelist)
        free (nodelist);
    if (request)
        json_object_put (request);
    return rc;
}

flux_mrpc_t flux_mrpc_create_fromevent (void *h, json_object *request)
{
    flux_mrpc_t f = xzmalloc (sizeof (*f));
    const char *tmp;

    if (util_json_object_get_string (request, "dest", &tmp) < 0) {
        msg ("%s: dest is missing", __FUNCTION__);
        goto error;
    }
    if (!(f->nodelist = hostlist_create (tmp))) {
        msg ("%s: error creating hostlist from %s", __FUNCTION__, tmp);
        goto error;
    }
    if (!nodelist_member (f->nodelist, flux_rank (h))) {
        msg ("%s: %d not a member of %s", __FUNCTION__, flux_rank(h), tmp);
        goto error;
    }
    f->nprocs = hostlist_count (f->nodelist);
    if (util_json_object_get_string (request, "path", &tmp) < 0) {
        msg ("%s: path is missing", __FUNCTION__);
        goto error;
    }
    f->path = xstrdup (tmp);
    if (util_json_object_get_int (request, "sender", &f->sender) < 0) {
        msg ("%s: sender is missing", __FUNCTION__);
        goto error;
    }
    if (util_json_object_get_int (request, "vers", &f->kvs_version) < 0) {
        msg ("%s: vers is missing", __FUNCTION__);
        goto error;
    }
    f->h = h;
    if (kvs_wait_version (h, f->kvs_version) < 0) {
        err ("%s: error waiting for kvs version %d", __FUNCTION__, f->kvs_version);
        goto error;
    }
    return f;
error:
    flux_mrpc_destroy (f);
    return NULL;
}

int flux_mrpc_respond (flux_mrpc_t f)
{
    return kvs_fence (f->h, f->path, f->nprocs + 1);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
