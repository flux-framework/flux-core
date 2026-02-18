#include <string.h>
#include <errno.h>
#include <flux/core.h>
#include <flux/jobtap.h>

static void my_cleanup (void *arg)
{
    flux_t *h = arg;

    flux_log (h, LOG_INFO, "job_aux test destructor invoked");
}

static int depend_cb (flux_plugin_t *p,
                      const char *topic,
                      flux_plugin_arg_t *args,
                      void *arg)
{
    int rc;
    void *val;
    flux_jobid_t id;
    flux_t *h = flux_jobtap_get_flux (p);

    /*  Test job_aux by jobid here since the current job will be active */

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I}",
                                "id", &id) < 0)
        return flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                            "test", 0,
                                            "failed to unpack jobid: %s",
                                            flux_plugin_arg_strerror (args));


    /*  test aux_set with jobid */
    rc = flux_jobtap_job_aux_set (p, id, "foo", p, NULL);
    if (rc != 0)
        return flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                            "test", 0,
                                            "flux_jobtap_aux_set failed: %s",
                                            strerror (errno));

    val = flux_jobtap_job_aux_get (p, id, "foo");
    if (!val || val != p)
        return flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                            "test", 0,
                                            "flux_jobtap_aux_get failed: %s",
                                            strerror (errno));

    rc = flux_jobtap_job_aux_delete_value (p, id, val);
    if (rc < 0)
        return flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                            "test", 0,
                                            "flux_jobtap_aux_delete_value"
                                            " failed: %s",
                                            strerror (errno));

    if (flux_jobtap_job_aux_get (p, id, "foo"))
        return flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                            "test", 0,
                                            "flux_jobtap_aux_get: %s",
                                            "unexpected success");

    /* Leave an entry for cleanup later */
    (void)flux_jobtap_job_aux_set (p, id, "foo", h, my_cleanup);


    return 0;
}

static int validate_cb (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *arg)
{
    void *val;
    /*  Test all job_aux() jobtap interfaces */
    int rc = flux_jobtap_job_aux_set (NULL,
                                      FLUX_JOBTAP_CURRENT_JOB,
                                      "foo",
                                      p,
                                      NULL);
    if (rc >= 0)
        return flux_jobtap_reject_job (p, args,
                                       "flux_jobtap_aux_set(NULL, ...) >= 0");
    if (errno != EINVAL)
        return flux_jobtap_reject_job (p, args,
                                       "flux_jobtap_aux_set(NULL, ...) "
                                       "expected errno == EINVAL");

    rc = flux_jobtap_job_aux_set (p, 1234, "foo", p, NULL);
    if (rc >= 0)
        return flux_jobtap_reject_job (p, args,
                                       "flux_jobtap_aux_set(p, 1234,...) >= 0");
    if (errno != ENOENT)
        return flux_jobtap_reject_job (p, args,
                                       "flux_jobtap_aux_set(NULL, ...) "
                                       "expected errno == EINVAL, got %d",
                                       errno);

    /*  test aux_set with current job */
    rc = flux_jobtap_job_aux_set (p,
                                  FLUX_JOBTAP_CURRENT_JOB,
                                  "foo",
                                  p,
                                  NULL);
    if (rc != 0)
        return flux_jobtap_reject_job (p, args,
                                       "flux_jobtap_aux_set() failed: %s",
                                       strerror (errno));

    val = flux_jobtap_job_aux_get (p, FLUX_JOBTAP_CURRENT_JOB, "foo");
    if (!val || val != p)
        return flux_jobtap_reject_job (p, args,
                                       "flux_jobtap_aux_get() failed: %s",
                                       strerror (errno));

    rc = flux_jobtap_job_aux_delete_value (p, FLUX_JOBTAP_CURRENT_JOB, val);
    if (rc < 0)
        return flux_jobtap_reject_job (p, args,
                                       "flux_jobtap_aux_delete_value() failed:"
                                       " %s",
                                       strerror (errno));

    if (flux_jobtap_job_aux_get (p, FLUX_JOBTAP_CURRENT_JOB, "foo"))
        return flux_jobtap_reject_job (p, args,
                                       "flux_jobtap_aux_get(): %s",
                                       "unexpected success");

    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    if (flux_plugin_add_handler (p, "job.validate", validate_cb, NULL) < 0
        || flux_plugin_add_handler (p, "job.state.depend", depend_cb, NULL) < 0)
        return -1;
    return 0;
}

// vi:ts=4 sw=4 expandtab
