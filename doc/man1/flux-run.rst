.. flux-help-include: true
.. flux-help-section: submission

===========
flux-run(1)
===========


SYNOPSIS
========

**flux** **run** [OPTIONS] [*--ntasks=N*] COMMAND...


DESCRIPTION
===========

.. program:: flux run

:program:`flux run` submits a job to run interactively under Flux, blocking
until the job has completed.  The job consists of *N* copies of COMMAND
launched together as a parallel job.

If :option:`--ntasks` is unspecified, a value of *N=1* is assumed.

The available OPTIONS are detailed below.

JOB PARAMETERS
==============

These commands accept only the simplest parameters for expressing
the size of the parallel program and the geometry of its task slots:

Common resource options
-----------------------

These commands take the following common resource allocation options:

.. include:: common/job-param-common.rst

Per-task options
----------------

:man1:`flux-run`, :man1:`flux-submit` and :man1:`flux-bulksubmit` take two
sets of mutually exclusive options to specify the size of the job request.
The most common form uses the total number of tasks to run along with
the amount of resources required per task to specify the resources for
the entire job:

.. include:: common/job-param-pertask.rst

Per-resource options
--------------------

The second set of options allows an amount of resources to be specified
with the number of tasks per core or node set on the command line. It is
an error to specify any of these options when using any per-task option
listed above:

.. include:: common/job-param-perres.rst

Additional job options
----------------------

These commands also take following job parameters:

.. include:: common/job-param-additional.rst

STANDARD I/O
============

By default, task stdout and stderr streams are redirected to the
KVS, where they may be accessed with the ``flux job attach`` command.

In addition, :man1:`flux-run` processes standard I/O in real time,
emitting the job's I/O to its stdout and stderr.

.. include:: common/job-standard-io.rst

CONSTRAINTS
===========

.. include:: common/job-constraints.rst

DEPENDENCIES
============

.. include:: common/job-dependencies.rst

ENVIRONMENT
===========

By default, these commands duplicate the current environment when submitting
jobs. However, a set of environment manipulation options are provided to
give fine control over the requested environment submitted with the job.

.. include:: common/job-environment.rst

ENV RULES
---------

.. include:: common/job-env-rules.rst

PROCESS RESOURCE LIMITS
=======================

By default these commands propagate some common resource limits (as described
in :linux:man2:`getrlimit`) to the job by setting the ``rlimit`` job shell
option in jobspec.  The set of resource limits propagated can be controlled
via the :option:`--rlimit=RULE` option:

.. include:: common/job-process-resource-limits.rst

JOB ENVIRONMENT VARIABLES
=========================

The job environment is described in more detail in the :man7:`flux-environment`
:ref:`job_environment` section.  A summary of the most commonly used variables
is provided below:

.. include:: common/job-environment-variables.rst

EXIT STATUS
===========

The job exit status, normally the largest task exit status, is stored
in the KVS. If one or more tasks are terminated with a signal,
the job exit status is 128+signo.

The ``flux-job attach`` command exits with the job exit status.

In addition, :man1:`flux-run` runs until the job completes and exits
with the job exit status.

OTHER OPTIONS
=============

.. include:: common/job-other-options.rst

.. include:: common/job-other-run.rst

SHELL OPTIONS
=============

Some options that affect the parallel runtime environment are provided
by the Flux shell.  These options are described in detail in
:man7:`flux-shell-options`.  A list of the most commonly needed options
follows.

Usage: :option:`flux run -o NAME[=ARG]`.

.. make :option: references in the included table x-ref to flux-shell-options(7)
.. program:: flux shell options
.. include:: common/job-shell-options.rst
.. program:: flux run

MUSTACHE TEMPLATES
==================

.. include:: common/job-mustache-templates.rst

RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man1:`flux-submit`, :man1:`flux-alloc`, :man1:`flux-batch`,
:man1:`flux-bulksubmit`, :man7:`flux-environment`
