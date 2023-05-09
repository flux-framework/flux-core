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
    flux_jobid_t id;
    const char *test_mode;
    flux_t *h = flux_jobtap_get_flux (p);

    /*  Get test-mode argument from jobspec
     */
    if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_IN,
                                "{s:I s:{s:{s:{s:{s:s}}}}}",
                                "id", &id,
                                "jobspec",
                                 "attributes",
                                  "system",
                                   "jobtap",
                                    "test-mode", &test_mode) < 0) {
        flux_log (h, LOG_ERR,
                  "test: flux_plugin_arg_unpack: %s",
                  flux_plugin_arg_strerror (args));
        return -1;
    }

    if (streq (topic, "job.validate")) {
        if (streq (test_mode, "validate failure"))
            return flux_jobtap_reject_job (p, args, "rejected for testing");
        if (streq (test_mode, "validate failure nullmsg"))
            return flux_jobtap_reject_job (p, args, NULL);
        if (streq (test_mode, "validate failure nomsg"))
            return -1;
        return 0;
    }

    /*  Update annotations with the test mode:
     */
    if (flux_plugin_arg_pack (args,
                              FLUX_PLUGIN_ARG_OUT,
                              "{s:{s:s}}",
                              "annotations",
                              "test", test_mode) < 0)
        flux_log (h, LOG_ERR,
                  "arg_pack: %s",
                  flux_plugin_arg_strerror (args));

    if (streq (topic, "job.state.priority")) {
        if (streq (test_mode, "priority unset"))
            return 0;
        if (streq (test_mode, "callback error"))
            return -1;
        if (streq (test_mode, "annotations error")) {
            if (flux_plugin_arg_pack (args,
                                      FLUX_PLUGIN_ARG_OUT,
                                      "{s:s}",
                                      "annotations", "test") < 0)
                flux_log (h, LOG_ERR,
                          "arg_pack: %s",
                          flux_plugin_arg_strerror (args));
            return 0;
        }
        if (streq (test_mode, "priority type error")) {
            flux_plugin_arg_pack (args,
                                  FLUX_PLUGIN_ARG_OUT,
                                  "{s:s}",
                                  "priority", "foo");
        }
    }
    else if (streq (topic, "job.state.sched")) {
        if (streq (test_mode, "sched: priority unavail"))
            return flux_jobtap_priority_unavail (p, args);
        if (streq (test_mode, "sched: callback error"))
            return -1;
        if (streq (test_mode, "sched: update priority")) {
            flux_plugin_arg_pack (args,
                                  FLUX_PLUGIN_ARG_OUT,
                                  "{s:i}",
                                  "priority", 42);
        }
        if (streq (test_mode, "sched: dependency-add")) {
            return flux_jobtap_dependency_add (p, id, "foo");
        }
        if (streq (test_mode, "sched: exception")) {
            flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                        "test", 0,
                                        "sched: test exception");
        }
        if (streq (test_mode, "sched: exception error")) {
            if (flux_jobtap_raise_exception (NULL, 0, "test", 0, "") >= 0
                || errno != EINVAL)
                flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                            "test", 0,
                                            "sched: exception error failed");
        }
    }
    else if (streq (topic, "job.priority.get")) {
        if (streq (test_mode, "priority.get: fail"))
            return -1;
        if (streq (test_mode, "priority.get: unavail"))
            return flux_jobtap_priority_unavail (p, args);
        if (streq (test_mode, "priority.get: bad arg")) {
            flux_plugin_arg_pack (args,
                                  FLUX_PLUGIN_ARG_OUT,
                                  "{s:s}",
                                  "priority", "foo");
        }
    }
    return 0;
}

static void reprioritize_cb (flux_t *h, flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
{
    flux_plugin_t *p = arg;
    flux_log (h, LOG_INFO, "jobtap.test: reprioritizing all jobs");
    if (flux_jobtap_reprioritize_all (p) < 0)
        flux_log_error (h, "reprioritize");
    flux_log (h, LOG_INFO, "jobtap.test: reprioritizing all jobs complete");
    if (flux_respond (h, msg, "{}") < 0)
        flux_log_error (h, "flux_respond");
}

int flux_plugin_init (flux_plugin_t *p)
{
    flux_t *h = flux_jobtap_get_flux (p);
    flux_plugin_set_name (p, "test");

    /*  Print config if we got one */
    flux_log (h, LOG_INFO, "jobtap.test: conf=%s", flux_plugin_get_conf (p));

    /*  Allow reprioritization of all jobs via an RPC:
     */
    if (flux_jobtap_service_register (p,
                                      "reprioritize",
                                      reprioritize_cb, p) < 0) {
        flux_log_error (h, "jobtap_service_register");
        return -1;
    }
    return flux_plugin_add_handler (p, "job.*", cb, NULL);
}

// vi:ts=4 sw=4 expandtab
