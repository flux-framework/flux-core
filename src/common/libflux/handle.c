/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <limits.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <sys/epoll.h>
#include <poll.h>
#if HAVE_CALIPER
#include <caliper/cali.h>
#include <sys/syscall.h>
#endif

#include "src/common/libflux/plugin_private.h"
#include "src/common/librouter/rpc_track.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/dirwalk.h"
#include "src/common/libutil/aux.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libidset/idset.h"
#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"

#include "handle.h"
#include "reactor.h"
#include "connector.h"
#include "message.h"
#include "msg_handler.h" // for flux_sleep_on ()
#include "flog.h"
#include "conf.h"
#include "msg_deque.h"
#include "message_private.h" // to check msg refcount in flux_send_new ()

#if HAVE_CALIPER
struct profiling_context {
    int initialized;
    cali_id_t msg_type;
    cali_id_t msg_seq;
    cali_id_t msg_topic;
    cali_id_t msg_sender;
    cali_id_t msg_rpc;
    cali_id_t msg_rpc_nodeid;
    cali_id_t msg_rpc_resp_expected;
    cali_id_t msg_action;
    cali_id_t msg_match_type;
    cali_id_t msg_match_tag;
    cali_id_t msg_match_glob;
};
#endif

struct flux_handle {
    flux_t          *parent; // if FLUX_O_CLONE, my parent
    struct aux_item *aux;
    int             usecount;
    int             flags;

    /* element below are unused in cloned handles */
    const struct flux_handle_ops *ops;
    void            *impl;
    void            *dso;
    struct msg_deque *queue;
    int             pollfd;

    struct idset    *tagpool;
    flux_msgcounters_t msgcounters;
    flux_comms_error_f comms_error_cb;
    void            *comms_error_arg;
    bool            comms_error_in_progress;
    bool            destroy_in_progress;
#if HAVE_CALIPER
    struct profiling_context prof;
#endif
    struct rpc_track *tracker;
};

struct builtin_connector {
    const char *scheme;
    connector_init_f *init;
};

flux_t *connector_loop_init (const char *uri, int flags, flux_error_t *errp);
flux_t *connector_interthread_init (const char *uri,
                                    int flags,
                                    flux_error_t *errp);
flux_t *connector_local_init (const char *uri, int flags, flux_error_t *errp);

static struct builtin_connector builtin_connectors[] = {
    { "loop", &connector_loop_init },
    { "interthread", &connector_interthread_init },
    { "local", &connector_local_init },
};

static void handle_trace (flux_t *h, const char *fmt, ...)
    __attribute__ ((format (printf, 2, 3)));


static int validate_flags (int flags, int allowed)
{
    if ((flags & allowed) != flags) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static flux_t *lookup_clone_ancestor (flux_t *h)
{
    while ((h->flags & FLUX_O_CLONE))
        h = h->parent;
    return h;
}

#if HAVE_CALIPER
void profiling_context_init (struct profiling_context* prof)
{
    prof->msg_type = cali_create_attribute ("flux.message.type",
                                            CALI_TYPE_STRING,
                                            CALI_ATTR_DEFAULT | CALI_ATTR_ASVALUE);
    prof->msg_seq = cali_create_attribute ("flux.message.seq",
                                           CALI_TYPE_INT,
                                           CALI_ATTR_SKIP_EVENTS);
    prof->msg_topic = cali_create_attribute ("flux.message.topic",
                                             CALI_TYPE_STRING,
                                             CALI_ATTR_DEFAULT | CALI_ATTR_ASVALUE);
    prof->msg_sender = cali_create_attribute ("flux.message.sender",
                                              CALI_TYPE_STRING,
                                              CALI_ATTR_SKIP_EVENTS);
    // if flux.message.rpc is set, we're inside an RPC, it will be set to a
    // type, single or multi
    prof->msg_rpc = cali_create_attribute ("flux.message.rpc",
                                           CALI_TYPE_STRING,
                                           CALI_ATTR_SKIP_EVENTS);
    prof->msg_rpc_nodeid = cali_create_attribute ("flux.message.rpc.nodeid",
                                                  CALI_TYPE_INT,
                                                  CALI_ATTR_SKIP_EVENTS);
    prof->msg_rpc_resp_expected =
        cali_create_attribute ("flux.message.response_expected",
                               CALI_TYPE_INT,
                               CALI_ATTR_SKIP_EVENTS);
    prof->msg_action = cali_create_attribute ("flux.message.action",
                                              CALI_TYPE_STRING,
                                              CALI_ATTR_DEFAULT | CALI_ATTR_ASVALUE);
    prof->msg_match_type = cali_create_attribute ("flux.message.match.type",
                                                  CALI_TYPE_INT,
                                                  CALI_ATTR_SKIP_EVENTS);
    prof->msg_match_tag = cali_create_attribute ("flux.message.match.tag",
                                                 CALI_TYPE_INT,
                                                 CALI_ATTR_SKIP_EVENTS);
    prof->msg_match_glob = cali_create_attribute ("flux.message.match.glob",
                                                  CALI_TYPE_STRING,
                                                  CALI_ATTR_SKIP_EVENTS);
    prof->initialized=1;
}

static void profiling_msg_snapshot (flux_t *h,
                          const flux_msg_t *msg,
                          int flags,
                          const char *msg_action)
{
    h = lookup_clone_ancestor (h);
    cali_id_t attributes[3];
    const void * data[3];
    size_t size[3];

    // This can get called before the handle is really ready
    if(! h->prof.initialized) return;

    int len = 0;

    if (msg_action) {
        attributes[len] = h->prof.msg_action;
        data[len] = msg_action;
        size[len] = strlen(msg_action);
        ++len;
    }

    int type;
    flux_msg_get_type (msg, &type);
    const char *msg_type = flux_msg_typestr (type);
    if (msg_type) {
        attributes[len] = h->prof.msg_type;
        data[len] = msg_type;
        size[len] = strlen(msg_type);
        ++len;
    }

    const char *msg_topic;
    if (type != FLUX_MSGTYPE_CONTROL)
        flux_msg_get_topic (msg, &msg_topic);
    else
        msg_topic = "NONE";
    /* attributes[len] = h->prof.msg_topic; */
    /* data[len] = msg_topic; */
    /* size[len] = strlen(msg_topic); */
    /* ++len; */

    if (type == FLUX_MSGTYPE_EVENT) {
        uint32_t seq;
        flux_msg_get_seq (msg, &seq);
        cali_begin_int (h->prof.msg_seq, seq);
    }
    cali_push_snapshot (CALI_SCOPE_PROCESS | CALI_SCOPE_THREAD,
                        len /* n_entries */,
                        attributes /* event_attributes */,
                        data /* event_data */,
                        size /* event_size */);
    if (type == FLUX_MSGTYPE_EVENT)
        cali_end (h->prof.msg_seq);
}


#endif

static connector_init_f *find_connector_builtin (const char *scheme)
{
    for (int i = 0; i < ARRAY_SIZE (builtin_connectors); i++)
        if (streq (scheme, builtin_connectors[i].scheme))
            return builtin_connectors[i].init;
    return NULL;
}

static char *find_file (const char *name, const char *searchpath)
{
    char *path;
    zlist_t *l;
    if (!(l = dirwalk_find (searchpath,
                            DIRWALK_REALPATH | DIRWALK_NORECURSE,
                            name,
                            1,
                            NULL,
                            NULL)))
        return NULL;
    path = zlist_pop (l);
    zlist_destroy (&l);
    return path;
}

static connector_init_f *find_connector_dso (const char *scheme,
                                             void **dsop,
                                             flux_error_t *errp)
{
    char name[PATH_MAX];
    const char *searchpath = getenv ("FLUX_CONNECTOR_PATH");
    char *path = NULL;
    void *dso = NULL;
    connector_init_f *connector_init = NULL;

    if (!searchpath)
        searchpath = flux_conf_builtin_get ("connector_path", FLUX_CONF_AUTO);
    if (snprintf (name, sizeof (name), "%s.so", scheme) >= sizeof (name)) {
        errprintf (errp, "Connector path too long");
        errno = ENAMETOOLONG;
        return NULL;
    }
    if (!(path = find_file (name, searchpath))) {
        errprintf (errp, "Unable to find connector name '%s'", scheme);
        errno = ENOENT;
        goto error;
    }
    if (!(dso = dlopen (path, RTLD_LAZY | RTLD_LOCAL | plugin_deepbind ()))) {
        errprintf (errp, "dlopen: %s: %s", path, dlerror ());
        errno = EINVAL;
        goto error;
    }
    if (!(connector_init = dlsym (dso, "connector_init"))) {
        errprintf (errp, "%s: missing connector_init symbol", path);
        errno = EINVAL;
        goto error_dlopen;
    }
    *dsop = dso;
    free (path);
    return connector_init;
error_dlopen:
    ERRNO_SAFE_WRAP (dlclose, dso);
error:
    ERRNO_SAFE_WRAP (free, path);
    return NULL;
}

static char *strtrim (char *s, const char *trim)
{
    char *p = s + strlen (s) - 1;
    while (p >= s && strchr (trim, *p))
        *p-- = '\0';
    return *s ? s : NULL;
}

flux_t *flux_open_ex (const char *uri, int flags, flux_error_t *errp)
{
    char *default_uri = NULL;
    char *path = NULL;
    char *scheme = NULL;
    void *dso = NULL;
    connector_init_f *connector_init = NULL;
    const char *s;
    flux_t *h = NULL;
    const int valid_flags = FLUX_O_TRACE
                          | FLUX_O_CLONE
                          | FLUX_O_NONBLOCK
                          | FLUX_O_MATCHDEBUG
                          | FLUX_O_TEST_NOSUB
                          | FLUX_O_RPCTRACK
                          | FLUX_O_NOREQUEUE;

    err_init (errp);

    if (validate_flags (flags, valid_flags) < 0) {
        errprintf (errp, "invalid flags specified");
        return NULL;
    }

    /* Try to get URI from (in descending precedence):
     *   argument > environment > builtin
     */
    if (!uri)
        uri = getenv ("FLUX_URI");
    if (!uri) {
        if (asprintf (&default_uri, "local://%s/local",
                      flux_conf_builtin_get ("rundir",
                                             FLUX_CONF_INSTALLED)) < 0)
            goto error;
        uri = default_uri;
    }
    if (!(scheme = strdup (uri)))
        goto error;
    path = strstr (scheme, "://");
    if (path) {
        *path = '\0';
        path = strtrim (path + 3, " \t");
    }
    if (!(connector_init = find_connector_builtin (scheme))
        && !(connector_init = find_connector_dso (scheme, &dso, errp)))
        goto error;
    if (getenv ("FLUX_HANDLE_TRACE"))
        flags |= FLUX_O_TRACE;
    if (getenv ("FLUX_HANDLE_MATCHDEBUG"))
        flags |= FLUX_O_MATCHDEBUG;
    if (!(h = connector_init (path, flags, errp))) {
        if (dso)
            ERRNO_SAFE_WRAP (dlclose, dso);
        goto error;
    }
    h->dso = dso;
#if HAVE_CALIPER
    profiling_context_init(&h->prof);
#endif
    if ((s = getenv ("FLUX_HANDLE_USERID"))) {
        uint32_t userid = strtoul (s, NULL, 10);
        if (flux_opt_set (h,
                          FLUX_OPT_TESTING_USERID,
                          &userid,
                          sizeof (userid)) < 0)
            goto error_handle;
    }
    if ((s = getenv ("FLUX_HANDLE_ROLEMASK"))) {
        uint32_t rolemask = strtoul (s, NULL, 0);
        if (flux_opt_set (h,
                          FLUX_OPT_TESTING_ROLEMASK,
                          &rolemask,
                          sizeof (rolemask)) < 0)
            goto error_handle;
    }
    free (scheme);
    free (default_uri);
    return h;
error_handle:
    flux_handle_destroy (h);
error:
    ERRNO_SAFE_WRAP (free, scheme);
    ERRNO_SAFE_WRAP (free, default_uri);
    /*
     *  Fill errp->text with strerror only when a more specific error has not
     *   already been set.
     */
    if (errp && !strlen (errp->text))
        errprintf (errp, "%s", strerror (errno));
    return NULL;
}

flux_t *flux_open (const char *uri, int flags)
{
    return flux_open_ex (uri, flags, NULL);
}

void flux_close (flux_t *h)
{
    flux_handle_destroy (h);
}

/* Create an idset that is configured as an allocator per idset_alloc(3) to
 * be used as a matchtag allocator.  Remove FLUX_MATCHTAG_NONE from the pool.
 */
static struct idset *tagpool_create (void)
{
    struct idset *idset;
    int flags = IDSET_FLAG_AUTOGROW
              | IDSET_FLAG_INITFULL
              | IDSET_FLAG_COUNT_LAZY;

    if (!(idset = idset_create (0, flags))
        || idset_clear (idset, FLUX_MATCHTAG_NONE) < 0) {
        idset_destroy (idset);
        return NULL;
    }
    return idset;
}

/* Destroy matchtag allocator.  If 'debug' is true, then print something
 * if any tags were still allocated.
 */
static void tagpool_destroy (struct idset *idset, bool debug)
{
    if (idset) {
        int saved_errno = errno;
        if (debug) {
            idset_set (idset, FLUX_MATCHTAG_NONE);
            uint32_t count = idset_universe_size (idset) - idset_count (idset);
            if (count > 0) {
                fprintf (stderr,
                         "MATCHDEBUG: pool destroy with %d allocated\n",
                         count);
            }
        }
        idset_destroy (idset);
        errno = saved_errno;
    }
}

flux_t *flux_handle_create (void *impl,
                            const struct flux_handle_ops *ops,
                            int flags)
{
    flux_t *h;

    if (!(h = calloc (1, sizeof (*h))))
        return NULL;
    h->usecount = 1;
    h->flags = flags;
    h->ops = ops;
    h->impl = impl;
    if (!(h->tagpool = tagpool_create ()))
        goto error;
    if (!(flags & FLUX_O_NOREQUEUE)) {
        if (!(h->queue = msg_deque_create (MSG_DEQUE_SINGLE_THREAD)))
            goto error;
    }
    if ((flags & FLUX_O_RPCTRACK)) {
        /* N.B. rpc_track functions are safe to call with tracker == NULL,
         * so skipping creation here disables tracking without further ado.
         */
        if (!(h->tracker = rpc_track_create (MSG_HASH_TYPE_UUID_MATCHTAG)))
            goto error;
    }
    h->pollfd = -1;
    return h;
error:
    flux_handle_destroy (h);
    return NULL;
}

flux_t *flux_clone (flux_t *orig)
{
    flux_t *h;
    if (!orig) {
        errno = EINVAL;
        return NULL;
    }
    if (!(h = calloc (1, sizeof (*h))))
        return NULL;
    h->parent = flux_incref (orig);
    h->usecount = 1;
    h->flags = orig->flags | FLUX_O_CLONE;
    return h;
}

static void fail_tracked_request (const flux_msg_t *msg, void *arg)
{
    flux_t *h = arg;
    flux_msg_t *rep;
    const char *topic = "NULL";
    struct flux_msg_cred lies = { .userid = 0, .rolemask = FLUX_ROLE_OWNER };

    flux_msg_get_topic (msg, &topic);
    if (!(rep = flux_response_derive (msg, ECONNRESET))
        || flux_msg_set_string (rep, "RPC aborted due to broker reconnect") < 0
        || flux_msg_set_cred (rep, lies) < 0
        || flux_requeue (h, rep, FLUX_RQ_TAIL) < 0) {
        handle_trace (h,
                      "error responding to tracked rpc topic=%s: %s",
                      topic,
                      strerror (errno));
    }
    else
        handle_trace (h, "responded to tracked rpc topic=%s", topic);
    flux_msg_destroy (rep);
}

/* Call connector's reconnect method, if available.
 */
int flux_reconnect (flux_t *h)
{
    if (!h) {
        errno = EINVAL;
        return -1;
    }
    h = lookup_clone_ancestor (h);
    if (!h->ops->reconnect) {
        errno = ENOSYS;
        return -1;
    }
    /* Assume that ops->pollfd() returns -1 if it is not connected.
     * If connected, remove the soon to be disconnected fd from h->pollfd.
     */
    if (h->ops->pollfd) {
        int fd = h->ops->pollfd (h->impl);
        if (fd >= 0)
            (void)epoll_ctl (h->pollfd, EPOLL_CTL_DEL, fd, NULL);
    }
    handle_trace (h, "trying to reconnect");
    if (h->ops->reconnect (h->impl) < 0) {
        handle_trace (h, "reconnect failed");
        return -1;
    }
    /* Reconnect was successful, add new ops->pollfd() to h->pollfd.
     */
    if (h->ops->pollfd) {
        struct epoll_event ev = {
            .events = EPOLLET | EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP,
        };
        if ((ev.data.fd = h->ops->pollfd (h->impl)) < 0
            || epoll_ctl (h->pollfd, EPOLL_CTL_ADD, ev.data.fd, &ev) < 0)
            return -1;
    }
    handle_trace (h, "reconnected");
    rpc_track_purge (h->tracker, fail_tracked_request, h);
    return 0;
}

void flux_handle_destroy (flux_t *h)
{
    if (h && --h->usecount == 0) {
        int saved_errno = errno;
        if (h->destroy_in_progress)
            return;
        h->destroy_in_progress = true;
        rpc_track_destroy (h->tracker);
        aux_destroy (&h->aux);
        if ((h->flags & FLUX_O_CLONE)) {
            flux_handle_destroy (h->parent); // decr usecount
        }
        else {
            if (h->ops->impl_destroy)
                h->ops->impl_destroy (h->impl);
            tagpool_destroy (h->tagpool,
                             (h->flags & FLUX_O_MATCHDEBUG) ? true : false);
#ifndef __SANITIZE_ADDRESS__
            if (h->dso)
                dlclose (h->dso);
#endif
            msg_deque_destroy (h->queue);
        }
        free (h);
        errno = saved_errno;
    }
}

flux_t *flux_incref (flux_t *h)
{
    if (h)
        h->usecount++;
    return h;
}

void flux_decref (flux_t *h)
{
    flux_handle_destroy (h);
}

void flux_flags_set (flux_t *h, int flags)
{
    /* N.B. FLUX_O_RPCTRACK not permitted here, only in flux_open() */
    const int valid_flags = FLUX_O_TRACE
                          | FLUX_O_CLONE
                          | FLUX_O_NONBLOCK
                          | FLUX_O_MATCHDEBUG
                          | FLUX_O_TEST_NOSUB;
    h->flags |= (flags & valid_flags);
}

void flux_flags_unset (flux_t *h, int flags)
{
    h->flags &= ~flags;
}

int flux_flags_get (flux_t *h)
{
    return h->flags;
}

int flux_opt_get (flux_t *h, const char *option, void *val, size_t len)
{
    if (!h || !option) {
        errno = EINVAL;
        return -1;
    }
    h = lookup_clone_ancestor (h);
    if (!h->ops->getopt) {
        errno = EINVAL;
        return -1;
    }
    return h->ops->getopt (h->impl, option, val, len);
}

int flux_opt_set (flux_t *h, const char *option, const void *val, size_t len)
{
    if (!h || !option) {
        errno = EINVAL;
        return -1;
    }
    h = lookup_clone_ancestor (h);
    if (!h->ops->setopt) {
        errno = EINVAL;
        return -1;
    }
    return h->ops->setopt (h->impl, option, val, len);
}

void *flux_aux_get (flux_t *h, const char *name)
{
    if (!h) {
        errno = EINVAL;
        return NULL;
    }
    return aux_get (h->aux, name);
}

int flux_aux_set (flux_t *h, const char *name, void *aux, flux_free_f destroy)
{
    if (!h) {
        errno = EINVAL;
        return -1;
    }
    return aux_set (&h->aux, name, aux, destroy);
}

void flux_comms_error_set (flux_t *h, flux_comms_error_f fun, void *arg)
{
    h = lookup_clone_ancestor (h);
    h->comms_error_cb = fun;
    h->comms_error_arg = arg;
}

static int comms_error (flux_t *h, int errnum)
{
    int rc = -1;

    h = lookup_clone_ancestor (h);
    if (h->comms_error_cb && !h->comms_error_in_progress) {
        h->comms_error_in_progress = true;
        errno = errnum;
        rc = h->comms_error_cb (h, h->comms_error_arg);
        h->comms_error_in_progress = false;
    }
    return rc;
}

void flux_get_msgcounters (flux_t *h, flux_msgcounters_t *mcs)
{
    h = lookup_clone_ancestor (h);
    *mcs = h->msgcounters;
}

void flux_clr_msgcounters (flux_t *h)
{
    h = lookup_clone_ancestor (h);
    memset (&h->msgcounters, 0, sizeof (h->msgcounters));
}

uint32_t flux_matchtag_alloc (flux_t *h)
{
    if (!h)
        goto inval;
    h = lookup_clone_ancestor (h);
    if (!h->tagpool)
        goto inval;
    unsigned int id;
    size_t oldsize = idset_universe_size (h->tagpool);
    if (idset_alloc (h->tagpool, &id) < 0) {
        flux_log (h, LOG_ERR, "tagpool temporarily out of tags");
        return FLUX_MATCHTAG_NONE;
    }
    size_t newsize = idset_universe_size (h->tagpool);
    if (newsize > oldsize) {
        flux_log (h,
                  LOG_INFO,
                  "tagpool expanded from %zu to %zu entries",
                  oldsize,
                  newsize);
    }
    return id;
inval:
    errno = EINVAL;
    return FLUX_MATCHTAG_NONE;
}

void flux_matchtag_free (flux_t *h, uint32_t matchtag)
{
    if (!h)
        return;
    h = lookup_clone_ancestor (h);
    if (!h->tagpool)
        return;
    /* fast path */
    if (!(h->flags & FLUX_O_MATCHDEBUG)) {
        if (matchtag != FLUX_MATCHTAG_NONE)
            idset_free (h->tagpool, matchtag);
        return;
    }
    /* MATCHDEBUG path */
    if (matchtag == FLUX_MATCHTAG_NONE)
        fprintf (stderr, "MATCHDEBUG: invalid tag=%d", (int)matchtag);
    else if (idset_free_check (h->tagpool, matchtag) < 0) {
        if (errno == EEXIST)
            fprintf (stderr, "MATCHDEBUG: double free tag=%d", (int)matchtag);
        else
            fprintf (stderr, "MATCHDEBUG: invalid tag=%d", (int)matchtag);
    }
}

uint32_t flux_matchtag_avail (flux_t *h)
{
    if (!h)
        return 0;
    h = lookup_clone_ancestor (h);
    return h->tagpool ? idset_count (h->tagpool) : 0;
}

static void update_tx_stats (flux_t *h, const flux_msg_t *msg)
{
    int type;
    if (flux_msg_get_type (msg, &type) == 0) {
        switch (type) {
            case FLUX_MSGTYPE_REQUEST:
                h->msgcounters.request_tx++;
                break;
            case FLUX_MSGTYPE_RESPONSE:
                h->msgcounters.response_tx++;
                break;
            case FLUX_MSGTYPE_EVENT:
                h->msgcounters.event_tx++;
                break;
            case FLUX_MSGTYPE_CONTROL:
                h->msgcounters.control_tx++;
                break;
        }
    } else
        errno = 0;
}

static void update_rx_stats (flux_t *h, const flux_msg_t *msg)
{
    int type;
    if (flux_msg_get_type (msg, &type) == 0) {
        switch (type) {
            case FLUX_MSGTYPE_REQUEST:
                h->msgcounters.request_rx++;
                break;
            case FLUX_MSGTYPE_RESPONSE:
                h->msgcounters.response_rx++;
                break;
            case FLUX_MSGTYPE_EVENT:
                h->msgcounters.event_rx++;
                break;
        case FLUX_MSGTYPE_CONTROL:
            h->msgcounters.control_rx++;
            break;
        }
    } else
        errno = 0;
}

static double handle_trace_timestamp (flux_t *h)
{
    struct timespec *ts;
    const char *auxkey = "flux::trace_start";

    if (!(ts = flux_aux_get (h, auxkey))) {
        if ((ts = calloc (1, sizeof (*ts)))) {
            monotime (ts);
            if (flux_aux_set (h, auxkey, ts, free) < 0) {
                free (ts);
                ts = NULL;
            }
        }
    }
    return ts ? monotime_since (*ts)/1000 : -1;
}

static void handle_trace_message (flux_t *h, const flux_msg_t *msg)
{
    if ((h->flags & FLUX_O_TRACE)) {
        flux_msg_fprint_ts (stderr, msg, handle_trace_timestamp (h));
    }
}

static void handle_trace (flux_t *h, const char *fmt, ...)
{
    if ((h->flags & FLUX_O_TRACE)) {
        va_list ap;
        char buf[256];
        int saved_errno = errno;
        double timestamp = handle_trace_timestamp (h);

        va_start (ap, fmt);
        vsnprintf (buf, sizeof (buf), fmt, ap);
        va_end (ap);

        fprintf (stderr, "--------------------------------------\n");
        if (timestamp >= 0.)
            fprintf (stderr, "c %.5f\n", timestamp);
        fprintf (stderr, "c %s\n", buf);

        errno = saved_errno;
    }
}

int flux_send (flux_t *h, const flux_msg_t *msg, int flags)
{
    if (!h || !msg || validate_flags (flags, FLUX_O_NONBLOCK) < 0) {
        errno = EINVAL;
        return -1;
    }
    h = lookup_clone_ancestor (h);
    if (!h->ops->send || h->destroy_in_progress) {
        errno = ENOSYS;
        return -1;
    }
    flags |= h->flags;
    update_tx_stats (h, msg);
    handle_trace_message (h, msg);
#if HAVE_CALIPER
    profiling_msg_snapshot(h, msg, flags, "send");
#endif
    while (h->ops->send (h->impl, msg, flags) < 0) {
        if (comms_error (h, errno) < 0)
            return -1;
        /* retry if comms_error() returns success */
    }
    rpc_track_update (h->tracker, msg);
    return 0;
}

int flux_send_new (flux_t *h, flux_msg_t **msg, int flags)
{
    if (!h || !msg || !*msg || (*msg)->refcount > 1) {
        errno = EINVAL;
        return -1;
    }
    h = lookup_clone_ancestor (h);
    /* Fall back to flux_send() if there are complications to using
     * op->send_new(), e.g. when message is still needed after send.
     */
    if (!h->ops->send_new || h->tracker != NULL) {
        if (flux_send (h, *msg, flags) < 0)
            return -1;
        flux_msg_destroy (*msg);
        *msg = NULL;
        return 0;
    }
    if (h->destroy_in_progress) {
        errno = ENOSYS;
        return -1;
    }
    flags |= h->flags;
    update_tx_stats (h, *msg);
    handle_trace_message (h, *msg);
#if HAVE_CALIPER
    profiling_msg_snapshot(h, *msg, flags, "send");
#endif
    while (h->ops->send_new (h->impl, msg, flags) < 0) {
        if (comms_error (h, errno) < 0)
            return -1;
        /* retry if comms_error() returns success */
    }
    return 0;
}

static int defer_enqueue (zlist_t **l, flux_msg_t *msg)
{
    if ((!*l && !(*l = zlist_new ())) || zlist_append (*l, msg) < 0) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static int defer_requeue (zlist_t **l, flux_t *h)
{
    flux_msg_t *msg;
    if (*l) {
        while ((msg = zlist_pop (*l))) {
            int rc = flux_requeue (h, msg, FLUX_RQ_TAIL);
            flux_msg_destroy (msg);
            if (rc < 0)
                return -1;
        }
    }
    return 0;
}

static void defer_destroy (zlist_t **l)
{
    if (*l) {
        flux_msg_t *msg;
        int saved_errno = errno;;
        while ((msg = zlist_pop (*l)))
            flux_msg_destroy (msg);
        zlist_destroy (l);
        errno = saved_errno;
    }
}

static flux_msg_t *flux_recv_any (flux_t *h, int flags)
{
    flux_msg_t *msg;

    if (!(h->flags & FLUX_O_NOREQUEUE)) {
        if (!msg_deque_empty (h->queue))
            return msg_deque_pop_front (h->queue);
    }
    if (!h->ops->recv) {
        errno = ENOSYS;
        return NULL;
    }
    while (!(msg = h->ops->recv (h->impl, flags))) {
        if (errno == EAGAIN
            || errno == EWOULDBLOCK
            || comms_error (h, errno) < 0)
            return NULL;
    }
    return msg;
}

/* N.B. the do loop below that reads messages and compares them to match
 * criteria may have to read a few non-matching messages before finding
 * a match.  On return, those non-matching messages have to be requeued
 * in the handle, hence the defer_*() helper calls.
 */
flux_msg_t *flux_recv (flux_t *h, struct flux_match match, int flags)
{
    if (!h || validate_flags (flags, FLUX_O_NONBLOCK) < 0) {
        errno = EINVAL;
        return NULL;
    }
    h = lookup_clone_ancestor (h);
    zlist_t *l = NULL;
    flux_msg_t *msg = NULL;

    flags |= h->flags;
    do {
        if (!(msg = flux_recv_any (h, flags))) {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                goto error;
            if (defer_requeue (&l, h) < 0)
                goto error;
            defer_destroy (&l);
            errno = EWOULDBLOCK;
            return NULL;
        }
        if (!flux_msg_cmp (msg, match)) {
            if (defer_enqueue (&l, msg) < 0)
                goto error;
            msg = NULL;
        }
    } while (!msg);
    update_rx_stats (h, msg);
    handle_trace_message (h, msg);
    if (defer_requeue (&l, h) < 0)
        goto error;
    defer_destroy (&l);
#if HAVE_CALIPER
    cali_begin_int (h->prof.msg_match_type, match.typemask);
    cali_begin_int (h->prof.msg_match_tag, match.matchtag);
    cali_begin_string (h->prof.msg_match_glob,
                       match.topic_glob ? match.topic_glob : "NONE");
    const char *sender;
    sender = flux_msg_route_first (msg);
    if (sender)
        cali_begin_string (h->prof.msg_sender, sender);
    profiling_msg_snapshot (h, msg, flags, "recv");
    if (sender)
        cali_end (h->prof.msg_sender);
    cali_end (h->prof.msg_match_type);
    cali_end (h->prof.msg_match_tag);
    cali_end (h->prof.msg_match_glob);
#endif
    rpc_track_update (h->tracker, msg);
    return msg;
error:
    flux_msg_destroy (msg);
    defer_destroy (&l);
    return NULL;
}

/* FIXME: FLUX_O_TRACE will show these messages being received again
 * So will message counters.
 */
int flux_requeue (flux_t *h, const flux_msg_t *msg, int flags)
{
    if (!h || !msg) {
        errno = EINVAL;
        return -1;
    }
    h = lookup_clone_ancestor (h);
    int rc;

    if ((h->flags & FLUX_O_NOREQUEUE)
        || (flags != FLUX_RQ_TAIL && flags != FLUX_RQ_HEAD)) {
        errno = EINVAL;
        return -1;
    }
    flux_msg_incref (msg);
    if (flags == FLUX_RQ_TAIL)
        rc = msg_deque_push_back (h->queue, (flux_msg_t *)msg);
    else
        rc = msg_deque_push_front (h->queue, (flux_msg_t *)msg);
    if (rc < 0)
        flux_msg_decref (msg);
    return rc;
}

int flux_pollfd (flux_t *h)
{
    h = lookup_clone_ancestor (h);

    if ((h->flags & FLUX_O_NOREQUEUE)) {
        if (!h->ops->pollfd) {
            errno = ENOTSUP;
            return -1;
        }
        return h->ops->pollfd (h->impl);
    }

    if (h->pollfd < 0) {
        struct epoll_event ev = {
            .events = EPOLLET | EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP,
        };
        if ((h->pollfd = epoll_create1 (EPOLL_CLOEXEC)) < 0)
            goto error;
        /* add queue pollfd */
        ev.data.fd = msg_deque_pollfd (h->queue);
        if (ev.data.fd < 0)
            goto error;
        if (epoll_ctl (h->pollfd, EPOLL_CTL_ADD, ev.data.fd, &ev) < 0)
            goto error;
        /* add connector pollfd (if defined) */
        if (h->ops->pollfd) {
            ev.data.fd = h->ops->pollfd (h->impl);
            if (ev.data.fd < 0)
                goto error;
            if (epoll_ctl (h->pollfd, EPOLL_CTL_ADD, ev.data.fd, &ev) < 0)
                goto error;
        }
    }
    return h->pollfd;
error:
    if (h->pollfd >= 0) {
        (void)close (h->pollfd);
        h->pollfd = -1;
    }
    return -1;
}

int flux_pollevents (flux_t *h)
{
    h = lookup_clone_ancestor (h);
    int e, events = 0;

    if (!(h->flags & FLUX_O_NOREQUEUE)) {
        /* wait for handle event */
        if (h->pollfd >= 0) {
            struct epoll_event ev;
            (void)epoll_wait (h->pollfd, &ev, 1, 0);
        }
    }
    /* get connector events (if applicable) */
    if (h->ops->pollevents) {
        if ((events = h->ops->pollevents (h->impl)) < 0)
            return -1;
        if ((events & FLUX_POLLERR)) {
            if (comms_error (h, ECONNRESET) == 0)
                events &= ~FLUX_POLLERR;
        }
    }
    if (!(h->flags & FLUX_O_NOREQUEUE)) {
        /* get queue events */
        if ((e = msg_deque_pollevents (h->queue)) < 0)
            return -1;
        if ((e & POLLIN))
            events |= FLUX_POLLIN;
        if ((e & POLLOUT))
            events |= FLUX_POLLOUT;
        if ((e & POLLERR))
            events |= FLUX_POLLERR;
    }
    return events;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
