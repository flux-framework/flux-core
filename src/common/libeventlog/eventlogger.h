/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_FLUX_EVENTLOGGER_H
#define HAVE_FLUX_EVENTLOGGER_H

#include <stdarg.h>
#include <flux/core.h>
#include <jansson.h>

#ifdef __cplusplus
extern "C" {
#endif

struct eventlogger;

typedef void (*eventlogger_state_f) (struct eventlogger *ev, void *arg);
typedef void (*eventlogger_err_f) (struct eventlogger *ev,
                                   void *arg,
                                   int err,
                                   json_t *entry);

struct eventlogger_ops {
    eventlogger_state_f busy; /* Called after idle when starting a batch    */
    eventlogger_state_f idle; /* Called when no more batches pending        */
    eventlogger_err_f  err;   /* Called on error, once per failed entry     */
};

enum {
    EVENTLOGGER_FLAG_ASYNC = 0,  /* Append entry to eventlog asynchronously */
    EVENTLOGGER_FLAG_WAIT =  1,  /* Append entry to eventlog synchronously  */
};

#define EVENTLOGGER_MAX_APPEND 16384

/*  Create an eventlogger with batched eventlog appends at interval
 *   `timeout`. Eventlogger will process user callbacks in `ops` as
 *   defined above.
 */
struct eventlogger *eventlogger_create (flux_t *h,
                                        double timeout,
                                        struct eventlogger_ops *ops,
                                        void *arg);

/*  Set eventlogger namespace
 */
int eventlogger_setns (struct eventlogger *ev, const char *ns);

void eventlogger_destroy (struct eventlogger *ev);

int eventlogger_append (struct eventlogger *ev,
                        int flags,
                        const char *path,
                        const char *name,
                        const char *context);

int eventlogger_append_entry (struct eventlogger *ev,
                              int flags,
                              const char *path,
                              json_t *entry);

int eventlogger_append_vpack (struct eventlogger *ev,
                              int flags,
                              const char *path,
                              const char *name,
                              const char *fmt,
                              va_list ap);

int eventlogger_append_pack (struct eventlogger *ev,
                             int flags,
                             const char *path,
                             const char *name,
                             const char *fmt, ...);

int eventlogger_set_commit_timeout (struct eventlogger *ev, double timeout);

int eventlogger_flush (struct eventlogger *ev);

flux_future_t *eventlogger_commit (struct eventlogger *ev);

#ifdef __cplusplus
}
#endif

#endif /* !HAVE_FLUX_EVENTLOGGER_H */
