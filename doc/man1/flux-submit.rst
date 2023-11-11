.. flux-help-include: true
.. flux-help-section: submission

==============
flux-submit(1)
==============


SYNOPSIS
========

**flux** **submit** [OPTIONS] [*--ntasks=N*] COMMAND...


DESCRIPTION
===========

.. program:: flux submit

:program:`flux submit` enqueues a job to run under Flux and prints its
numerical jobid on standard output.  The job consists of *N* copies of COMMAND
launched together as a parallel job.

If :option:`--ntasks` is unspecified, a value of *N=1* is assumed.

The available OPTIONS are detailed below.

.. include:: common/job-parameters.rst

.. include:: common/job-standard-io.rst

.. include:: common/job-constraints.rst

.. include:: common/job-dependencies.rst

.. include:: common/job-environment.rst

.. include:: common/job-process-resource-limits.rst

.. include:: common/job-environment-variables.rst

.. include:: common/job-exit-status.rst

.. include:: common/job-other-options.rst

.. include:: common/job-shell-options.rst


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man1:`flux-run`, :man1:`flux-alloc`, :man1:`flux-batch`,
:man1:`flux-bulksubmit`
