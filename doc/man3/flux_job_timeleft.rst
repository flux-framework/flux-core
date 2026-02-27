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

The :func:`flux_job_timeleft` function returns the remaining time available
to the calling process.  If the ``FLUX_JOB_ID`` environment variable is set,
the process is assumed to be part of a Flux job and the remaining time is
determined by querying the job's expiration from the job-list service.
Otherwise, the expiration is determined from the instance's resource set (R)
via the ``resource.status`` RPC, which works for any enclosing Flux instance
regardless of how it was launched, including instances started under a foreign
resource manager.

RETURN VALUE
============

:func:`flux_job_timeleft` returns 0 on success with the remaining time in
floating point seconds stored in :var:`timeleft`.  If the enclosing job or
instance does not have an established time limit, then :var:`timeleft` is set
to ``inf``.  If the time limit has expired or the job is no longer running,
then :var:`timeleft` is set to ``0``.

If an error occurs, then this function returns ``-1`` with an error string
set in ``error->text``.

RESOURCES
=========

.. include:: common/resources.rst
