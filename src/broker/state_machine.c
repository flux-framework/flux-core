/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
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
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libidset/idset.h"
#include "src/common/libhostlist/hostlist.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/array_size/array_size.h"

#include "state_machine.h"

#include "broker.h"
#include "runat.h"
#include "overlay.h"
#include "attr.h"
#include "groups.h"
#include "shutdown.h"

struct quorum {
    struct idset *want;
    struct idset *have; // cumulative on rank 0, batch buffer on rank > 0
    flux_future_t *f;
    double timeout;
    bool warned;
    flux_watcher_t *timer;
};

struct monitor {
    struct flux_msglist *requests;

    flux_future_t *f;
    broker_state_t parent_state;
    unsigned int parent_valid:1;
    unsigned int parent_error:1;
};

struct state_machine {
    struct broker *ctx;
    broker_state_t state;
    struct timespec t_start;

    zlist_t *events;
    flux_watcher_t *prep;
    flux_watcher_t *check;
    flux_watcher_t *idle;

    flux_msg_handler_t **handlers;

    struct monitor monitor;
    struct quorum quorum;

    struct flux_msglist *wait_requests;

    int exit_norestart;
};

typedef void (*action_f)(struct state_machine *s);

struct state {
    broker_state_t state;
    const char *name;
    action_f action;
};

struct state_next {
    const char *event;
    broker_state_t current;
    broker_state_t next;
};

static void action_join (struct state_machine *s);
static void action_quorum (struct state_machine *s);
static void action_init (struct state_machine *s);
static void action_run (struct state_machine *s);
static void action_cleanup (struct state_machine *s);
static void action_shutdown (struct state_machine *s);
static void action_finalize (struct state_machine *s);
static void action_exit (struct state_machine *s);

static void runat_completion_cb (struct runat *r, const char *name, void *arg);
static void monitor_update (flux_t *h,
                            struct flux_msglist *requests,
                            broker_state_t state);
static void wait_update (flux_t *h,
                         struct flux_msglist *requests,
                         broker_state_t state);
static void join_check_parent (struct state_machine *s);
static void quorum_check_parent (struct state_machine *s);
static void run_check_parent (struct state_machine *s);

static struct state statetab[] = {
    { STATE_NONE,       "none",             NULL },
    { STATE_JOIN,       "join",             action_join },
    { STATE_INIT,       "init",             action_init },
    { STATE_QUORUM,     "quorum",           action_quorum },
    { STATE_RUN,        "run",              action_run },
    { STATE_CLEANUP,    "cleanup",          action_cleanup },
    { STATE_SHUTDOWN,   "shutdown",         action_shutdown },
    { STATE_FINALIZE,   "finalize",         action_finalize },
    { STATE_GOODBYE,    "goodbye",          NULL },
    { STATE_EXIT,       "exit",             action_exit },
};

static struct state_next nexttab[] = {
    { "start",              STATE_NONE,         STATE_JOIN },
    { "parent-ready",       STATE_JOIN,         STATE_INIT },
    { "parent-none",        STATE_JOIN,         STATE_INIT },
    { "parent-fail",        STATE_JOIN,         STATE_SHUTDOWN },
    { "rc1-success",        STATE_INIT,         STATE_QUORUM },
    { "rc1-none",           STATE_INIT,         STATE_QUORUM },
    { "rc1-fail",           STATE_INIT,         STATE_SHUTDOWN},
    { "quorum-full",        STATE_QUORUM,       STATE_RUN },
    { "quorum-fail",        STATE_QUORUM,       STATE_SHUTDOWN},
    { "rc2-success",        STATE_RUN,          STATE_CLEANUP },
    { "rc2-fail",           STATE_RUN,          STATE_CLEANUP },
    { "shutdown",           STATE_RUN,          STATE_CLEANUP },
    { "rc2-none",           STATE_RUN,          STATE_RUN },
    { "cleanup-success",    STATE_CLEANUP,      STATE_SHUTDOWN },
    { "cleanup-none",       STATE_CLEANUP,      STATE_SHUTDOWN },
    { "cleanup-fail",       STATE_CLEANUP,      STATE_SHUTDOWN },
    { "children-complete",  STATE_SHUTDOWN,     STATE_FINALIZE },
    { "children-none",      STATE_SHUTDOWN,     STATE_FINALIZE },
    { "rc3-success",        STATE_FINALIZE,     STATE_GOODBYE },
    { "rc3-none",           STATE_FINALIZE,     STATE_GOODBYE },
    { "rc3-fail",           STATE_FINALIZE,     STATE_GOODBYE },
    { "goodbye",            STATE_GOODBYE,      STATE_EXIT },
};

static const double default_quorum_timeout = 60; // log slow joiners

static void state_action (struct state_machine *s, broker_state_t state)
{
    int i;
    for (i = 0; i < ARRAY_SIZE (statetab); i++) {
        if (statetab[i].state == state) {
            if (statetab[i].action)
                statetab[i].action (s);
            break;
        }
    }
}

static const char *statestr (broker_state_t state)
{
    int i;
    for (i = 0; i < ARRAY_SIZE (statetab); i++) {
        if (statetab[i].state == state)
            return statetab[i].name;
    }
    return "unknown";
}

static broker_state_t state_next (broker_state_t current, const char *event)
{
    int i;
    for (i = 0; i < ARRAY_SIZE (nexttab); i++) {
        if (nexttab[i].current == current && !strcmp (event, nexttab[i].event))
            return nexttab[i].next;
    }
    return current;
}

/* return true if a is a subset of b */
static bool is_subset_of (const struct idset *a, const struct idset *b)
{
    struct idset *ids;
    int count;

    if (!(ids = idset_difference (a, b)))
        return false;
    count = idset_count (ids);
    idset_destroy (ids);
    if (count > 0)
        return false;
    return true;
}

static void action_init (struct state_machine *s)
{
    s->ctx->online = true;
    if (runat_is_defined (s->ctx->runat, "rc1")) {
        if (runat_start (s->ctx->runat, "rc1", runat_completion_cb, s) < 0) {
            flux_log_error (s->ctx->h, "runat_start rc1");
            state_machine_post (s, "rc1-fail");
        }
    }
    else
        state_machine_post (s, "rc1-none");
}

static void action_join (struct state_machine *s)
{
    if (s->ctx->rank == 0)
        state_machine_post (s, "parent-none");
    else
        join_check_parent (s);
}

static void quorum_timer_cb (flux_reactor_t *r,
                             flux_watcher_t *w,
                             int revents,
                             void *arg)
{
    struct state_machine *s = arg;
    flux_t *h = s->ctx->h;
    struct idset *ids = NULL;
    char *rankstr = NULL;
    struct hostlist *hl = NULL;
    char *hoststr = NULL;
    unsigned int rank;

    if (s->state != STATE_QUORUM)
        return;

    if (!(ids = idset_difference (s->quorum.want, s->quorum.have))
        || !(rankstr = idset_encode (ids, IDSET_FLAG_RANGE))
        || !(hl = hostlist_create ())) {
        flux_log_error (h, "error computing slow brokers");
        goto done;
    }
    rank = idset_first (ids);
    while (rank != IDSET_INVALID_ID) {
        if (hostlist_append (hl, flux_get_hostbyrank (s->ctx->h, rank)) < 0) {
            flux_log_error (h, "error building slow brokers hostlist");
            goto done;
        }
        rank = idset_next (ids, rank);
    }
    if (!(hoststr = hostlist_encode (hl))) {
        flux_log_error (h, "error encoding slow brokers hostlist");
        goto done;
    }
    flux_log (s->ctx->h,
              LOG_ERR,
              "quorum delayed: waiting for %s (rank %s)",
              hoststr,
              rankstr);
    flux_timer_watcher_reset (w, s->quorum.timeout, 0.);
    flux_watcher_start (w);
    s->quorum.warned = true;
done:
    free (hoststr);
    hostlist_destroy (hl);
    free (rankstr);
    idset_destroy (ids);
}

static void action_quorum_continuation (flux_future_t *f, void *arg)
{
    struct state_machine *s = arg;

    if (flux_rpc_get (f, NULL) < 0)
        state_machine_post (s, "quorum-fail");
    flux_future_destroy (f);
}

static void action_quorum (struct state_machine *s)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (s->ctx->h,
                             "groups.join",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s}",
                             "name", "broker.online"))
        || flux_future_then (f, -1, action_quorum_continuation, s) < 0) {
        flux_future_destroy (f);
        state_machine_post (s, "quorum-fail");
        return;
    }
    if (s->ctx->rank > 0)
        quorum_check_parent (s);
    else if (s->quorum.timeout > 0.) {
        flux_timer_watcher_reset (s->quorum.timer, s->quorum.timeout, 0.);
        flux_watcher_start (s->quorum.timer);
    }
}

static void action_run (struct state_machine *s)
{
    if (runat_is_defined (s->ctx->runat, "rc2")) {
        if (runat_start (s->ctx->runat, "rc2", runat_completion_cb, s) < 0) {
            flux_log_error (s->ctx->h, "runat_start rc2");
            state_machine_post (s, "rc2-fail");
        }
    }
    else if (s->ctx->rank > 0)
        run_check_parent (s);
    else
        state_machine_post (s, "rc2-none");
}

static void action_cleanup (struct state_machine *s)
{
    if (runat_is_defined (s->ctx->runat, "cleanup")) {
        if (runat_start (s->ctx->runat, "cleanup", runat_completion_cb, s) < 0) {
            flux_log_error (s->ctx->h, "runat_start cleanup");
            state_machine_post (s, "cleanup-fail");
        }
    }
    else
        state_machine_post (s, "cleanup-none");
}

static void action_finalize (struct state_machine *s)
{
    overlay_shutdown (s->ctx->overlay);
    if (runat_is_defined (s->ctx->runat, "rc3")) {
        if (runat_start (s->ctx->runat, "rc3", runat_completion_cb, s) < 0) {
            flux_log_error (s->ctx->h, "runat_start rc3");
            state_machine_post (s, "rc3-fail");
        }
    }
    else
        state_machine_post (s, "rc3-none");
}

static void action_shutdown (struct state_machine *s)
{
    if (overlay_get_child_peer_count (s->ctx->overlay) == 0)
        state_machine_post (s, "children-none");
}

static void rmmod_continuation (flux_future_t *f, void *arg)
{
    struct state_machine *s = arg;

    if (flux_rpc_get (f, NULL) < 0)
        flux_log_error (s->ctx->h, "broker.rmmod connector-local");
    flux_reactor_stop (flux_get_reactor (s->ctx->h));
    flux_future_destroy (f);
}

/* Unload connector-local module, then stop the broker's reactor.
 */
static void action_exit (struct state_machine *s)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (s->ctx->h,
                             "broker.rmmod",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s}",
                             "name",
                             "connector-local"))
            || flux_future_then (f, -1, rmmod_continuation, s) < 0) {
        flux_log_error (s->ctx->h, "error sending broker.rmmod connector-local");
        flux_future_destroy (f);
        flux_reactor_stop (flux_get_reactor (s->ctx->h));
    }
}

static void process_event (struct state_machine *s, const char *event)
{
    broker_state_t next_state;

    next_state = state_next (s->state, event);

    if (next_state != s->state) {
        char fsd[64];
        fsd_format_duration (fsd,
                             sizeof (fsd),
                             monotime_since (s->t_start) * 1E-3);
        flux_log (s->ctx->h,
                  LOG_INFO, "%s: %s->%s %s",
                  event,
                  statestr (s->state),
                  statestr (next_state),
                  fsd);
        monotime (&s->t_start);
        s->state = next_state;
        state_action (s, s->state);
        monitor_update (s->ctx->h, s->monitor.requests, s->state);
        wait_update (s->ctx->h, s->wait_requests, s->state);
    }
    else {
        flux_log (s->ctx->h,
                  LOG_DEBUG,
                  "%s: ignored in %s",
                  event,
                  statestr (s->state));
    }
}

void state_machine_post (struct state_machine *s, const char *event)
{
    if (zlist_append (s->events, (char *)event) < 0)
        flux_log (s->ctx->h, LOG_ERR, "state_machine_post %s failed", event);
}

void state_machine_kill (struct state_machine *s, int signum)
{
    flux_t *h = s->ctx->h;

    switch (s->state) {
        case STATE_INIT:
            if (runat_abort (s->ctx->runat, "rc1") < 0)
                flux_log_error (h, "runat_abort rc1 (signal %d)", signum);
            break;
        case STATE_JOIN:
            state_machine_post (s, "parent-fail");
            break;
        case STATE_QUORUM:
            state_machine_post (s, "quorum-fail");
            break;
        case STATE_RUN:
            if (runat_is_defined (s->ctx->runat, "rc2")) {
                if (runat_abort (s->ctx->runat, "rc2") < 0)
                    flux_log_error (h, "runat_abort rc2 (signal %d)", signum);
            }
            else
                state_machine_post (s, "shutdown");
            break;
        case STATE_CLEANUP:
            if (runat_abort (s->ctx->runat, "cleanup") < 0)
                flux_log_error (h, "runat_abort cleanup (signal %d)", signum);
            break;
        case STATE_FINALIZE:
            (void)runat_abort (s->ctx->runat, "rc3");
            break;
        case STATE_NONE:
        case STATE_SHUTDOWN:
        case STATE_GOODBYE:
        case STATE_EXIT:
            flux_log (h,
                      LOG_INFO,
                      "ignored signal %d in %s",
                      signum,
                      statestr (s->state));
            break;
    }
}

int state_machine_shutdown (struct state_machine *s, flux_error_t *error)
{
    if (s->state != STATE_RUN) {
        errprintf (error,
                   "shutdown cannot be initiated in state %s",
                   statestr (s->state));
        errno = EINVAL;
        return -1;
    }
    if (s->ctx->rank != 0) {
        errprintf (error, "shutdown may only be initiated on rank 0");
        errno = EINVAL;
        return -1;
    }

    if (s->exit_norestart > 0)
        s->ctx->exit_rc = s->exit_norestart;

    if (runat_is_defined (s->ctx->runat, "rc2")) {
        if (runat_abort (s->ctx->runat, "rc2") < 0)
            flux_log_error (s->ctx->h, "runat_abort rc2 (shutdown)");
    }
    else
        state_machine_post (s, "shutdown");
    return 0;
}

static void runat_completion_cb (struct runat *r, const char *name, void *arg)
{
    struct state_machine *s = arg;
    int rc = 1;

    if (runat_get_exit_code (r, name, &rc) < 0)
        log_err ("runat_get_exit_code %s", name);

    if (!strcmp (name, "rc1")) {
        /* If rc1 fails, it most likely will fail again on restart, so if
         * running under systemd, exit with the broker.exit-norestart value.
         */
        if (rc != 0) {
            if (s->exit_norestart != 0)
                s->ctx->exit_rc = s->exit_norestart;
            else
                s->ctx->exit_rc = rc;
        }
        state_machine_post (s, rc == 0 ? "rc1-success" : "rc1-fail");
    }
    else if (!strcmp (name, "rc2")) {
        if (s->ctx->exit_rc == 0 && rc != 0)
            s->ctx->exit_rc = rc;
        state_machine_post (s, rc == 0 ? "rc2-success" : "rc2-fail");
    }
    else if (!strcmp (name, "cleanup")) {
        if (s->ctx->exit_rc == 0 && rc != 0)
            s->ctx->exit_rc = rc;
        state_machine_post (s, rc == 0 ? "cleanup-success" : "cleanup-fail");
    }
    else if (!strcmp (name, "rc3")) {
        if (s->ctx->exit_rc == 0 && rc != 0)
            s->ctx->exit_rc = rc;
        state_machine_post (s, rc == 0 ? "rc3-success" : "rc3-fail");
    }
}

/* If '-Sbroker.exit-norestart' was set on the command line, set
 * s->exit_norestart to its value; otherwise leave it set it to 0.
 */
static void norestart_configure (struct state_machine *s)
{
    const char *val;

    if (attr_get (s->ctx->attrs, "broker.exit-norestart", &val, NULL) == 0) {
        errno = 0;
        int rc = strtol (val, NULL, 10);
        if (errno == 0 && rc >= 1)
            s->exit_norestart = rc;
    }
}

static void prep_cb (flux_reactor_t *r,
                     flux_watcher_t *w,
                     int revents,
                     void *arg)
{
    struct state_machine *s = arg;

    if (zlist_size (s->events) > 0)
        flux_watcher_start (s->idle);
}

static void check_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    struct state_machine *s = arg;
    char *event;

    if ((event = zlist_pop (s->events))) {
        process_event (s, event);
        free (event);
    }
    flux_watcher_stop (s->idle);
}

static void run_check_parent (struct state_machine *s)
{
    if (s->monitor.parent_error)
        state_machine_post (s, "parent-fail");
    else if (s->monitor.parent_valid) {
        switch (s->monitor.parent_state) {
            case STATE_NONE:
            case STATE_JOIN:
            case STATE_INIT:
            case STATE_QUORUM:
            case STATE_RUN:
            case STATE_CLEANUP:
                break;
            case STATE_SHUTDOWN:
                state_machine_post (s, "shutdown");
                break;
            case STATE_FINALIZE:
            case STATE_GOODBYE:
            case STATE_EXIT:
                state_machine_post (s, "parent-fail");
                break;
        }
    }
}

/* Assumes local state is STATE_JOIN.
 * If parent has left STATE_INIT, post parent-ready or parent-fail.
 */
static void join_check_parent (struct state_machine *s)
{
    if (s->monitor.parent_error)
        state_machine_post (s, "parent-fail");
    else if (s->monitor.parent_valid) {
        switch (s->monitor.parent_state) {
            case STATE_NONE:
            case STATE_JOIN:
            case STATE_INIT:
                break;
            case STATE_QUORUM:
            case STATE_RUN:
                state_machine_post (s, "parent-ready");
                break;
            case STATE_CLEANUP:
            case STATE_SHUTDOWN:
            case STATE_FINALIZE:
            case STATE_GOODBYE:
            case STATE_EXIT:
                state_machine_post (s, "parent-fail");
                break;
        }
    }
}

/* Assumes local state is STATE_QUORUM.
 * If parent has left STATE_QUORUM, post quorum-full or quorum-fail.
 */
static void quorum_check_parent (struct state_machine *s)
{
    if (s->monitor.parent_error)
        state_machine_post (s, "quorum-fail");
    else if (s->monitor.parent_valid) {
        switch (s->monitor.parent_state) {
            case STATE_NONE:
            case STATE_JOIN:
            case STATE_QUORUM:
                break;
            case STATE_INIT:
            case STATE_RUN:
                state_machine_post (s, "quorum-full");
                break;
            case STATE_CLEANUP:
            case STATE_SHUTDOWN:
            case STATE_FINALIZE:
            case STATE_GOODBYE:
            case STATE_EXIT:
                state_machine_post (s, "quorum-fail");
                break;
        }
    }
}

/* Configure the set of broker ranks needed for quorum (default=all).
 */
static int quorum_configure (struct state_machine *s)
{
    const char *val;
    char *tmp;
    unsigned long id;

    if (attr_get (s->ctx->attrs, "broker.quorum", &val, NULL) == 0) {
        if (!(s->quorum.want = idset_decode (val))) {
            log_msg ("Error parsing broker.quorum attribute");
            return -1;
        }
        id = idset_last (s->quorum.want);
        if (id != IDSET_INVALID_ID && id >= s->ctx->size) {
            log_msg ("Error parsing broker.quorum attribute: exceeds size");
            return -1;
        }
        if (attr_delete (s->ctx->attrs, "broker.quorum", true) < 0)
            return -1;
    }
    else {
        if (!(s->quorum.want = idset_create (s->ctx->size, 0)))
            return -1;
        if (idset_range_set (s->quorum.want, 0, s->ctx->size - 1) < 0)
            return -1;
    }
    if (!(tmp = idset_encode (s->quorum.want, IDSET_FLAG_RANGE)))
        return -1;
    if (attr_add (s->ctx->attrs,
                  "broker.quorum",
                  tmp,
                  FLUX_ATTRFLAG_IMMUTABLE) < 0) {
        ERRNO_SAFE_WRAP (free, tmp);
        return -1;
    }
    free (tmp);
    return 0;
}

static int quorum_timeout_configure (struct state_machine *s)
{
    const char *name = "broker.quorum-timeout";
    const char *val;
    char fsd[32];

    if (attr_get (s->ctx->attrs, name, &val, NULL) == 0) {
        if (!strcmp (val, "none"))
            s->quorum.timeout = -1;
        else {
            if (fsd_parse_duration (val, &s->quorum.timeout) < 0) {
                log_msg ("Error parsing %s attribute", name);
                return -1;
            }
        }
        if (attr_delete (s->ctx->attrs, name, true) < 0)
            return -1;
    }
    else
        s->quorum.timeout = default_quorum_timeout;
    if (s->quorum.timeout == -1)
        snprintf (fsd, sizeof (fsd), "none");
    else {
        if (fsd_format_duration (fsd, sizeof (fsd), s->quorum.timeout) < 0)
            return -1;
    }
    if (attr_add (s->ctx->attrs, name, fsd, FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    return 0;
}

/* called on rank 0 only */
static void broker_online_cb (flux_future_t *f, void *arg)
{
    struct state_machine *s = arg;
    const char *members;
    struct idset *ids;
    static double last_update = 0;
    double now = flux_reactor_now (flux_get_reactor (s->ctx->h));
    bool quorum_reached = false;

    if (flux_rpc_get_unpack (f, "{s:s}", "members", &members) < 0
        || !(ids = idset_decode (members))) {
        flux_log_error (s->ctx->h, "groups.get failed");
        state_machine_post (s, "quorum-fail");
        return;
    }

    idset_destroy (s->quorum.have);
    s->quorum.have = ids;
    if (is_subset_of (s->quorum.want, s->quorum.have)) {
        quorum_reached = true;
    }

    if (strlen (members) > 0
        && (quorum_reached || now - last_update > 5)) {
        char *hosts = flux_hostmap_lookup (s->ctx->h, members, NULL);
        flux_log (s->ctx->h, LOG_INFO, "online: %s (ranks %s)", hosts, members);
        free (hosts);
        last_update = now;
    }

    if (quorum_reached) {
        if (s->state != STATE_RUN) {
            state_machine_post (s, "quorum-full");
            if (s->quorum.warned) {
                flux_log (s->ctx->h, LOG_ERR, "quorum reached");
                s->quorum.warned = false;
            }
        }
    }
    flux_future_reset (f);
}

static bool wait_respond (flux_t *h,
                          const flux_msg_t *msg,
                          broker_state_t state)
{
    int rc;

    if (state < STATE_RUN)
        return false;
    if (state == STATE_RUN)
        rc = flux_respond (h, msg, NULL);
    else {
        rc = flux_respond_error (h,
                                 msg,
                                 ENOENT,
                                 "broker has surpassed RUN state");
    }
    if (rc < 0)
        flux_log_error (h, "error responding to state-machine.wait request");
    return true;
}

static void wait_update (flux_t *h,
                         struct flux_msglist *requests,
                         broker_state_t state)
{
    const flux_msg_t *msg;

    msg = flux_msglist_first (requests);
    while (msg) {
        if (wait_respond (h, msg, state))
            flux_msglist_delete (requests);
        msg = flux_msglist_next (requests);
    }
}

/* This request is answered once the local broker enters RUN state.
 * An error response is generated if the local broker enters a state
 * that cannot lead to the run state, e.g. CLEANUP, SHUTDOWN, FINALIZE, EXIT.
 * This is handy when a running broker client tries to reconnect after a broker
 * restart.  If it tries to send requests too early, it may receive "Upstream
 * broker is offline" errors.  This request is specifically excluded from that
 * error path.
 */
static void state_machine_wait_cb (flux_t *h,
                                   flux_msg_handler_t *mh,
                                   const flux_msg_t *msg,
                                   void *arg)
{
    struct state_machine *s = arg;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (!wait_respond (h, msg, s->state)) {
        if (flux_msglist_append (s->wait_requests, msg) < 0)
            goto error;
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to state-machine.wait request");
}

static void log_monitor_respond_error (flux_t *h)
{
    if (errno != EHOSTUNREACH && errno != ENOSYS)
        flux_log_error (h, "error responding to state-machine.monitor request");
}

/* Return true if request should continue to receive updates
 */
static bool monitor_update_one (flux_t *h,
                                const flux_msg_t *msg,
                                broker_state_t state)
{
    broker_state_t final;

    if (flux_msg_unpack (msg, "{s:i}", "final", &final) < 0)
        final = STATE_EXIT;
    if (state > final)
        goto nodata;
    if (flux_respond_pack (h, msg, "{s:i}", "state", state) < 0)
        log_monitor_respond_error (h);
    if (!flux_msg_is_streaming (msg))
        return false;
    if (state == final)
        goto nodata;
    return true;
nodata:
    if (flux_respond_error (h, msg, ENODATA, NULL) < 0)
        log_monitor_respond_error (h);
    return false;
}

static void monitor_update (flux_t *h,
                            struct flux_msglist *requests,
                            broker_state_t state)
{
    const flux_msg_t *msg;

    msg = flux_msglist_first (requests);
    while (msg) {
        if (!monitor_update_one (h, msg, state))
            flux_msglist_delete (requests);
        msg = flux_msglist_next (requests);
    }
}

static void state_machine_monitor_cb (flux_t *h,
                                      flux_msg_handler_t *mh,
                                      const flux_msg_t *msg,
                                      void *arg)
{
    struct state_machine *s = arg;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (monitor_update_one (h, msg, s->state)) {
        if (flux_msglist_append (s->monitor.requests, msg) < 0)
            goto error;
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        log_monitor_respond_error (h);
}

static void monitor_continuation (flux_future_t *f, void *arg)
{
    struct state_machine *s = arg;
    flux_t *h = s->ctx->h;
    int state;

    if (flux_rpc_get_unpack (f, "{s:i}", "state", &state) < 0) {
        if (errno != ENODATA) {
            flux_log_error (h, "state-machine.monitor");
            s->monitor.parent_error = 1;
        }
        return;
    }
    s->monitor.parent_state = state;
    s->monitor.parent_valid = 1;
    flux_future_reset (f);
    if (s->state == STATE_JOIN)
        join_check_parent (s);
    else if (s->state == STATE_QUORUM)
        quorum_check_parent (s);
    else if (s->state == STATE_RUN)
        run_check_parent (s);
}

/* Set up monitoring of parent state up to and including SHUTDOWN state.
 * Skip monitoring states beyond that to avoid deadlock on disconnecting
 * children on zeromq-4.1.4 (doesn't seem to be a problem on newer versions).
 * State machine doesn't need to know about parent transition to these
 * states anyway.
 */
static flux_future_t *monitor_parent (flux_t *h, void *arg)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (h,
                             "state-machine.monitor",
                             FLUX_NODEID_UPSTREAM,
                             FLUX_RPC_STREAMING,
                             "{s:i}",
                             "final", STATE_SHUTDOWN)))
        return NULL;
    if (flux_future_then (f, -1, monitor_continuation, arg) < 0) {
        flux_future_destroy (f);
        return NULL;
    }
    return f;
}

/* This callback is called when the overlay connection state has changed.
 */
static void overlay_monitor_cb (struct overlay *overlay,
                                uint32_t rank,
                                void *arg)
{
    struct state_machine *s = arg;

    switch (s->state) {
        /* IN JOIN state, post parent-fail if something goes wrong with the
         * parent TBON connection.
         */
        case STATE_JOIN:
            if (overlay_parent_error (overlay)) {
                s->ctx->exit_rc = 1;
                state_machine_post (s, "parent-fail");
            }
            break;
        case STATE_RUN:
            if (overlay_parent_error (overlay)) {
                s->ctx->exit_rc = 1;
                state_machine_post (s, "shutdown");
            }
            break;
        /* In SHUTDOWN state, post exit event if children have disconnected.
         * If there are no children on entry to SHUTDOWN state (e.g. leaf
         * node) the exit event is posted immediately in action_shutdown().
         */
        case STATE_SHUTDOWN:
            if (overlay_get_child_peer_count (overlay) == 0)
                state_machine_post (s, "children-complete");
            break;
        default:
            break;
    }
}

static void state_machine_get_cb (flux_t *h,
                                  flux_msg_handler_t *mh,
                                  const flux_msg_t *msg,
                                  void *arg)
{
    struct state_machine *s = arg;
    double duration = monotime_since (s->t_start) * 1E-3;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (flux_respond_pack (h,
                           msg,
                           "{s:s s:f}",
                           "state", statestr (s->state),
                           "duration", duration) < 0)
        flux_log_error (h,
                        "error responding to state-machine.get request");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0) {
        flux_log_error (h,
                        "error responding to state-machine.get request");
    }
}

/* If a disconnect is received for streaming monitor request,
 * drop the request.
 */
static void disconnect_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct state_machine *s = arg;

    if (flux_msglist_disconnect (s->monitor.requests, msg) < 0
        || flux_msglist_disconnect (s->wait_requests, msg) < 0)
        flux_log_error (h, "error handling state-machine.disconnect");
}

static const struct flux_msg_handler_spec htab[] = {
    {    FLUX_MSGTYPE_REQUEST,
        "state-machine.monitor",
        state_machine_monitor_cb,
        0
    },
    {   FLUX_MSGTYPE_REQUEST,
        "state-machine.wait",
        state_machine_wait_cb,
        FLUX_ROLE_USER
    },
    {   FLUX_MSGTYPE_REQUEST,
        "state-machine.disconnect",
        disconnect_cb,
        0
    },
    {    FLUX_MSGTYPE_REQUEST,
        "state-machine.get",
        state_machine_get_cb,
        FLUX_ROLE_USER,
    },
    FLUX_MSGHANDLER_TABLE_END,
};

void state_machine_destroy (struct state_machine *s)
{
    if (s) {
        int saved_errno = errno;
        zlist_destroy (&s->events);
        flux_watcher_destroy (s->prep);
        flux_watcher_destroy (s->check);
        flux_watcher_destroy (s->idle);
        flux_msg_handler_delvec (s->handlers);
        flux_msglist_destroy (s->wait_requests);
        flux_future_destroy (s->monitor.f);
        flux_msglist_destroy (s->monitor.requests);
        idset_destroy (s->quorum.want);
        idset_destroy (s->quorum.have);
        flux_watcher_destroy (s->quorum.timer);
        flux_future_destroy (s->quorum.f);
        free (s);
        errno = saved_errno;
    }
}

struct state_machine *state_machine_create (struct broker *ctx)
{
    struct state_machine *s;
    flux_reactor_t *r = flux_get_reactor (ctx->h);

    if (!(s = calloc (1, sizeof (*s))))
        return NULL;
    s->ctx = ctx;
    s->state = STATE_NONE;
    monotime (&s->t_start);
    if (!(s->events = zlist_new ()))
        goto nomem;
    zlist_autofree (s->events);
    if (flux_msg_handler_addvec (ctx->h, htab, s, &s->handlers) < 0)
        goto error;
    if (!(s->wait_requests = flux_msglist_create ()))
        goto error;
    s->prep = flux_prepare_watcher_create (r, prep_cb, s);
    s->check = flux_check_watcher_create (r, check_cb, s);
    s->idle = flux_idle_watcher_create (r, NULL, NULL);
    s->quorum.timer = flux_timer_watcher_create (r, 0., 0., quorum_timer_cb, s);
    if (!s->prep || !s->check || !s->idle || !s->quorum.timer)
        goto nomem;
    flux_watcher_start (s->prep);
    flux_watcher_start (s->check);
    if (!(s->monitor.requests = flux_msglist_create ()))
        goto error;
    if (ctx->rank > 0) {
        if (!(s->monitor.f = monitor_parent (ctx->h, s)))
            goto error;
    }
    if (!(s->quorum.have = idset_create (ctx->size, 0)))
        goto error;
    if (quorum_configure (s) < 0
        || quorum_timeout_configure (s) < 0) {
        log_err ("error configuring quorum attributes");
        goto error;
    }
    norestart_configure (s);
    overlay_set_monitor_cb (ctx->overlay, overlay_monitor_cb, s);
    if (s->ctx->rank == 0) {
        if (!(s->quorum.f = flux_rpc_pack (ctx->h,
                                           "groups.get",
                                           FLUX_NODEID_ANY,
                                           FLUX_RPC_STREAMING,
                                           "{s:s}",
                                           "name", "broker.online"))
            || flux_future_then (s->quorum.f, -1, broker_online_cb, s) < 0)
            goto error;
    }
    return s;
nomem:
    errno = ENOMEM;
error:
    state_machine_destroy (s);
    return NULL;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
