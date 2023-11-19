====================
flux_job_timeleft(3)
====================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   int flux_job_timeleft (flux_t *h,
                          flux_error_t *error,
                          double *timeleft);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

The :func:`flux_job_timeleft` function determines if the calling process
is executing within the context of a Flux job (either a parallel job or
a Flux instance running as a job), then handles querying the appropriate
service for the remaining time in the job.

RETURN VALUE
============

:func:`flux_job_timeleft` returns 0 on success with the remaining time in
floating point seconds stored in :var:`timeleft`. If the job does not have
an established time limit, then :var:`timeleft` is set to ``inf``. If the job
time limit has expired or the job is no longer running, then :var:`timeleft`
is set to ``0``.

If the current process is not part of an active job or instance, or another
error occurs, then this function returns ``-1`` with an error string set in
``error->text``.

RESOURCES
=========

Flux: http://flux-framework.org
