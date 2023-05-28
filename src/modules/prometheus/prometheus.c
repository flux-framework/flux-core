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
#include <jansson.h>
#include <flux/core.h>
#include <assert.h>

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/errprintf.h"

#include "src/common/libcontent/content-util.h"

#include "src/common/libtomlc99/toml.h"
#include "src/common/libutil/tomltk.h"

#include "src/common/libyuarel/yuarel.h"
#include "ccan/str/str.h"

#include "microhttpd.h"
#include "prom.h"
#include "promhttp.h"

#include "microhttpd.h"
#include "prom.h"
#include "promhttp.h"

// Two gagues to simply count the number of jobs in the queue
// If we want to store status this could be a histogram type instead
prom_gauge_t *waiting_jobs;
prom_gauge_t *active_jobs;

// The daemon! Nothing is scarier than a daemon that might ask you to the prom.
// Oh right, this is for monitoring with Prometheus.
struct MHD_Daemon *prom_daemon;

// Register a metric for count of waiting jobs
void waiting_jobs_init(void) {
    waiting_jobs = prom_collector_registry_must_register_metric(

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
void active_jobs_init(void) {
    active_jobs = prom_collector_registry_must_register_metric(
        prom_gauge_new(
            "active_jobs",
            "the number of active jobs in the queue",
            1,
            (const char *[]) {"label"}
        )
    );
}

// Destroy the registry daemon. This deallocates all metrics too.
int stop_prom_daemon(void) {
  prom_collector_registry_destroy(PROM_COLLECTOR_REGISTRY_DEFAULT);
  PROM_COLLECTOR_REGISTRY_DEFAULT = NULL;

  // Stop the HTTP server
  MHD_stop_daemon(prom_daemon);

  return 0;
}


// TODO we maybe want a histogram to keep track of buckets
// of job types / resources or similar https://prometheus.io/docs/concepts/metric_types/#histogram
// There is also the counter type, which can get incremented

int mod_main (flux_t *h, int argc, char **argv)
{
    int rc = -1;

    printf("\nthis is the prometheus module running\n");

    // Does this mean the module gets unloaded?
    // If so, we can stop the server here
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done_unreg;
        goto stop_daemon;
    }

    // init the registry and each metric
    // we do this only if the flux connection is successful
    prom_collector_registry_default_init();
    active_jobs_init();
    waiting_jobs_init();

    // Set the active registry for the HTTP handler and create the daemon
    // Do we want to customize the port here somehow?
    // TODO figure out how to interact with this thing.
    promhttp_set_active_collector_registry(NULL);
    prom_daemon = promhttp_start_daemon(MHD_USE_SELECT_INTERNALLY, 8000, NULL, NULL);
    if (prom_daemon == NULL) {
        printf("There was an error starting the Prometheus daemon.");
        return rc;
    }

    // TODO update the metrics here from the Flux queue / reactor?
    // Discuss what metrics / format we are interested in
    // Discuss how/when this gets triggered (likely only on init and we need handles elsewhere?)
    // How do we get the queue metrics?
    rc = 0;

done_unreg:
    (void)content_unregister_backing_store (h);
stop_daemon:
    (void)stop_prom_daemon();

    // End of the function - return!
    return rc;
}
