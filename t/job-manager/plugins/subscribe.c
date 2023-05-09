#include <stdio.h>
#include <jansson.h>

#include <flux/core.h>
#include <flux/jobtap.h>
#include "ccan/str/str.h"

static int cb (flux_plugin_t *p,
               const char *topic,
               flux_plugin_arg_t *args,
               void *arg)
{
    flux_t *h = flux_jobtap_get_flux (p);

    if (streq (topic, "job.event.start")) {
        /*  Test flux_jobtap_job_event_posted(), then unsusbscribe()
         */
        if (flux_jobtap_job_event_posted (NULL, 0, NULL) != -1
            || flux_jobtap_job_event_posted (p, 0, NULL) != -1)
            flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                         "subscribe-test",
                                         0,
                                         "event_count() invalid args failed");
        if (flux_jobtap_job_event_posted (p,
                                         FLUX_JOBTAP_CURRENT_JOB,
                                         "start") != 1)
            flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                         "subscribe-test",
                                         0,
                                         "event_count 'start' didn't return 1");
        flux_jobtap_job_unsubscribe (p, FLUX_JOBTAP_CURRENT_JOB);
    }
    else if (streq (topic, "job.event.finish")) {
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "subscribe-test",
                                     0,
                                     "unexpectedly got finish event",
                                     strerror (errno));
        return -1;
    }

    flux_log (h, LOG_INFO, "subscribe-check: %s: OK", topic);
    if (streq (topic, "job.event.start")) {
        // Test for nonzero exit from job.event.* callback:
        return -1;
    }
    else
        return 0;
}

static int new_cb (flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *args,
                   void *arg)
{
    /*  Test invalid arguments */
    flux_jobtap_job_unsubscribe (NULL, 0);
    flux_jobtap_job_unsubscribe (p, 0);
    if (flux_jobtap_job_subscribe (NULL, 0) != -1
        || flux_jobtap_job_subscribe (p, 0) != -1)
        return flux_jobtap_reject_job (p, args,
                                       "subscribe-test: "
                                       "invalid args check failed");
    if (flux_jobtap_job_subscribe (p, FLUX_JOBTAP_CURRENT_JOB) < 0)
        return flux_jobtap_reject_job (p, args,
                                       "subscribe-test: "
                                       "flux_jobtap_job_subscribe: %s",
                                       strerror (errno));
    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    flux_t *h = flux_jobtap_get_flux (p);
    flux_plugin_set_name (p, "subscribe-test");

    if (flux_plugin_add_handler (p, "job.event.*", cb, NULL) < 0
        || flux_plugin_add_handler (p, "job.validate", new_cb, NULL) < 0) {
        flux_log_error (h, "flux_plugin_add_handler");
        return -1;
    }
    return 0;
}

// vi:ts=4 sw=4 expandtab
