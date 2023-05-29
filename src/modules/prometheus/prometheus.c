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
#include <assert.h>
#include "ccan/str/str.h"

#include "microhttpd.h"
#include "prom.h"
#include "promhttp.h"


//#include "src/common/libczmqcontainers/czmq_containers.h"
typedef struct prom_ctx prom_ctx_t;

// A prometheus context to hold our gauges and state
// This will get added to the Flux handle context
struct prom_ctx {
    flux_t *h;
    flux_watcher_t *timer;
    flux_msg_handler_t **handlers;
    flux_future_t *f;

    // Metrics go here!
    // Two gagues to simply count the number of jobs in the queue
    // If we want to store status this could be a histogram type instead
    int prom_port;
    prom_gauge_t *waiting_jobs;
    prom_gauge_t *active_jobs;

    // The daemon! Nothing is scarier than a daemon that might ask you to the prom.
    // Oh right, this is for monitoring with Prometheus.
    struct MHD_Daemon *prom_daemon;
};



// Register a metric for count of waiting jobs
void waiting_jobs_init(prom_ctx_t *ctx) {
    ctx->waiting_jobs = prom_collector_registry_must_register_metric(

        // metric name, help, buckets, and label keys
        prom_gauge_new(
            "waiting_jobs",
            "the number of waiting jobs in the queue",
            1,
            (const char *[]) {"jobs", "waiting"}
        )
    );
}

// Register a metric for count of active (running) jobs
void active_jobs_init(prom_ctx_t *ctx) {
    ctx->active_jobs = prom_collector_registry_must_register_metric(
        prom_gauge_new(
            "active_jobs",
            "the number of active jobs in the queue",
            1,
            (const char *[]) {"jobs", "active"}
        )
    );
}

// Destroy the registry daemon. This deallocates all metrics too.
int stop_prom_daemon(prom_ctx_t *ctx) {
  prom_collector_registry_destroy(PROM_COLLECTOR_REGISTRY_DEFAULT);
  PROM_COLLECTOR_REGISTRY_DEFAULT = NULL;

  // Stop the HTTP server
  MHD_stop_daemon(ctx->prom_daemon);

  return 0;
}

// Destroy the prom context and stop the daemon
static void prom_ctx_destroy (prom_ctx_t *ctx) {
    if (ctx == NULL)
        return;
    stop_prom_daemon(ctx);
    free (ctx);
}


// Example function to update metrics that appear at endpoint
// TODO these don't appear to be showing up at endpoint
// Likely there is some issue with saving state or daemon persisting
static void update_metrics(prom_ctx_t *ctx) {
    for (int i = 0; i < 100; i++) {
        prom_gauge_inc(ctx->waiting_jobs, NULL);
    }
    prom_gauge_add(ctx->active_jobs, 100, (const char *[]) { "active" });
    prom_gauge_add(ctx->waiting_jobs, 10, (const char *[]) { "active" });
} 

// Create the new Prometheus context
static prom_ctx_t * prom_ctx_create (flux_t *h) {
    prom_ctx_t *ctx = calloc (1, sizeof (*ctx));
    if (ctx == NULL) {
        flux_log_error (h, "prom_ctx_create");
        goto error;
    }

    // Set additional variables on the context here
    // Default for port if not set by user on command line
    if (&ctx->prom_port == NULL) {
        ctx->prom_port = 8000;
    }

    // init the registry and each metric
    // we do this only if the flux connection is successful
    // Not sure if I need better error handling in here
    if (ctx->prom_daemon == NULL) {
        prom_collector_registry_default_init();
        active_jobs_init(ctx);
        waiting_jobs_init(ctx);

        // Set the active registry for the HTTP handler and create the daemon
        // Do we want to customize the port here somehow?
        // TODO figure out how to interact with this thing.
        promhttp_set_active_collector_registry(NULL);
        ctx->prom_daemon = promhttp_start_daemon(MHD_USE_SELECT_INTERNALLY, 8000, NULL, NULL);
        if (ctx->prom_daemon == NULL) {
            printf("There was an error starting the Prometheus daemon.");
            goto error;
        }
    }

    // Set the flux handle on our context
    ctx->h = h;
    return ctx;

// Womp womp you FAILED
error:
    prom_ctx_destroy (ctx);
    return (NULL);
}


// Process arguments to the module, we currently only support port=
static void process_args (prom_ctx_t *ctx, int ac, char **av) {
    int i;
    for (i = 0; i < ac; i++) {
        if (strstarts (av[i], "port=")) {
            int port = atoi((av[i])+5);
            ctx->prom_port = port;
        }  else
            flux_log (ctx->h, LOG_ERR, "Unknown option `%s'", av[i]);
    }
}

// TODO we maybe want a histogram to keep track of buckets
// of job types / resources or similar https://prometheus.io/docs/concepts/metric_types/#histogram
// There is also the counter type, which can get incremented

int mod_main (flux_t *h, int argc, char **argv) {
    int rc = -1;

    printf("This is the prometheus module running\n");

    prom_ctx_t *ctx = prom_ctx_create (h);
    if (ctx == NULL) {
        printf("There was an issue creating the Prometheus context.");
        return rc;
    }

    // Set port for daemon
    process_args (ctx, argc, argv);

    // Does this mean the module gets unloaded?
    // If so, we can stop the server here
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done_unreg;
    }

    // TODO update the metrics here from the Flux queue / reactor?
    update_metrics(ctx);

    // Discuss what metrics / format we are interested in
    // Discuss how/when this gets triggered?
    // Is it logical to have the daemon start here?
    // How do we get the queue metrics (probably from the handle...)
    rc = 0;

done_unreg:
    (void)prom_ctx_destroy(ctx);

    // End of the function - return!
    return rc;
}
