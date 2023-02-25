.. flux-help-include: true

===========
flux-run(1)
===========


SYNOPSIS
========

**flux** **run** [OPTIONS] [*--ntasks=N*] COMMAND...


DESCRIPTION
===========

flux-run(1) submits a job to run interactively under Flux, blocking until
the job has completed.  The job consists of *N* copies of COMMAND launched
together as a parallel job.

If *--ntasks* is unspecified, a value of *N=1* is assumed.

The available OPTIONS are detailed below.

.. include:: common/submit-job-parameters.rst

.. include:: common/submit-standard-io.rst

.. include:: common/submit-constraints.rst

.. include:: common/submit-dependencies.rst

.. include:: common/submit-environment.rst

.. include:: common/submit-process-resource-limits.rst

.. include:: common/submit-exit-status.rst

.. include:: common/submit-other-options.rst

.. include:: common/submit-shell-options.rst


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man1:`flux-submit`, :man1:`flux-alloc`, :man1:`flux-batch`,
:man1:`flux-bulksubmit`
