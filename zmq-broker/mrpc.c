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
#include "flux.h"
#include "kvs.h"

#define KVS_CLEANUP 1

struct flux_mrpc_struct {
    flux_t h;
    zuuid_t *uuid;
    char *path;
    char *dest;
    int nprocs;
    int sender;
    int vers;
    hostlist_t hl;
    hostlist_iterator_t itr;
    bool client;
};

static bool dest_valid (hostlist_t hl, int maxid)
{
    hostlist_iterator_t itr = NULL;
    char *node;
    int nodeid;
    bool valid = false;

    hostlist_uniq (hl);
    if (hostlist_count (hl) < 1)
        goto done;
    itr = hostlist_iterator_create (hl);
    while ((node = hostlist_next (itr))) {
        nodeid = strtoul (node, NULL, 10);
        if (nodeid < 0 || nodeid > maxid)
            goto done;
    }
    valid = true;
done:
    if (itr)
        hostlist_iterator_destroy (itr);
    return valid;
}

static bool dest_member (hostlist_t hl, int node)
{
    char s[16];
    snprintf (s, sizeof (s), "%d", node);
    if (hostlist_find (hl, s) < 0)
        return false;
    return true;
}

flux_mrpc_t flux_mrpc_create (flux_t h, const char *dest)
{
    flux_mrpc_t f = xzmalloc (sizeof (*f));
    int maxid = flux_size (h) - 1;

    f->h = h;
    f->client = true;
    f->sender = flux_rank (h);
    f->dest = xstrdup (dest);
    if (!(f->hl = hostlist_create (dest)) || !dest_valid (f->hl, maxid)) {
        errno = EINVAL;
        goto error;
    }
    f->nprocs = hostlist_count (f->hl);
    if (!(f->uuid = zuuid_new ()))
        oom ();
    if (asprintf (&f->path, "mrpc.%s", zuuid_str (f->uuid)) < 0)
        oom ();

    return f;
error:
    flux_mrpc_destroy (f);
    return NULL;
}

void flux_mrpc_destroy (flux_mrpc_t f)
{
    if (f->path) {
#if KVS_CLEANUP
        if (f->client) {
            if (kvs_unlink (f->h, f->path) < 0)
                err ("kvs_unlink %s", f->path);
            if (kvs_commit (f->h) < 0)
                err ("kvs_commit");
        }
#endif
        free (f->path);
    }
    if (f->uuid)
        zuuid_destroy (&f->uuid);
    if (f->itr)
        hostlist_iterator_destroy (f->itr);
    if (f->hl)
        hostlist_destroy (f->hl);
    if (f->dest)
        free (f->dest);
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
    char *node;

    if (!f->itr)
        f->itr = hostlist_iterator_create (f->hl);
    if ((node = hostlist_next (f->itr)))
        return strtoul (node, NULL, 10);
    return -1;
}

void flux_mrpc_rewind_outarg (flux_mrpc_t f)
{
    if (f->itr)
        hostlist_iterator_reset (f->itr);
}

int flux_mrpc (flux_mrpc_t f, const char *fmt, ...)
{
    int rc = -1;
    char *tag = NULL;
    va_list ap;
    json_object *request = util_json_object_new_object ();

    va_start (ap, fmt);
    if (vasprintf (&tag, fmt, ap) < 0)
        oom ();
    va_end (ap);

    if (kvs_commit (f->h) < 0)
        goto done;
    if (kvs_get_version (f->h, &f->vers) < 0)
        goto done;
    util_json_object_add_string (request, "dest", f->dest);
    util_json_object_add_int (request, "vers", f->vers);
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
    if (request)
        json_object_put (request);
    return rc;
}

flux_mrpc_t flux_mrpc_create_fromevent (flux_t h, json_object *request)
{
    flux_mrpc_t f = NULL;
    const char *dest, *path;
    int sender, vers;
    hostlist_t hl = NULL;

    if (util_json_object_get_string (request, "dest", &dest) < 0
            || util_json_object_get_string (request, "path", &path) < 0
            || util_json_object_get_int (request, "sender", &sender) < 0
            || util_json_object_get_int (request, "vers", &vers) < 0
            || !(hl = hostlist_create (dest))) {
        errno = EPROTO;
        goto done;
    }
    if (!dest_member (hl, flux_rank (h))) {
        errno = EINVAL;
        goto done;
    }
    if (kvs_wait_version (h, vers) < 0)
        goto done;

    f = xzmalloc (sizeof (*f));
    f->h = h;
    f->client = false;
    f->nprocs = hostlist_count (hl);
    f->path = xstrdup (path);
    f->sender = sender;
    f->dest = xstrdup (dest);
    f->vers = vers;
    f->hl = hl;
    hl = NULL;
done:
    if (hl)
        hostlist_destroy (hl);
    return f;
}

int flux_mrpc_respond (flux_mrpc_t f)
{
    return kvs_fence (f->h, f->path, f->nprocs + 1);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
