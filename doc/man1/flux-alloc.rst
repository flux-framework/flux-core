.. flux-help-include: true
.. flux-help-section: submission

=============
flux-alloc(1)
=============

SYNOPSIS
========

**flux** **alloc** [OPTIONS] *--nslots=N* [COMMAND...]

DESCRIPTION
===========

.. program:: flux alloc

:program:`flux alloc` runs a Flux subinstance with *COMMAND* as the initial
program.  Once resources are allocated, *COMMAND* executes on the first node of
the allocation with any free arguments supplied as *COMMAND* arguments.  When
*COMMAND* exits, the Flux subinstance exits, resources are released to the
enclosing Flux instance, and :program:`flux alloc` returns.

If no *COMMAND* is specified, an interactive shell is spawned as the initial
program, and the subinstance runs until the shell is exited.

If the :option:`--bg` option is specified, the subinstance runs without an
initial program.  :program:`flux alloc` prints the jobid and returns as soon as
the subinstance is ready to accept jobs.  The subinstance runs until it exceeds
its time limit, is canceled, or is shut down with :man1:`flux-shutdown`.

Flux commands that are run from the subinstance (e.g. from the interactive
shell) refer to the subinstance. For example, :man1:`flux-run` would launch
work there.  A Flux command run from the subinstance can be forced to refer
to the enclosing instance by supplying the :option:`flux --parent` option.

Flux commands outside of the subinstance refer to their enclosing instance,
often a system instance. :man1:`flux-proxy` establishes a connection to a
running subinstance by jobid, then spawns a shell in which Flux commands
refer to the subinstance, for example

::

   $ flux alloc --bg -N 2 --queue=batch
   ƒM7Zq9AKHno
   $ flux proxy ƒM7Zq9AKHno
   $ flux mini run -n16 ./testprog
   ...
   $ flux shutdown
   ...
   $

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

Batch job options
-----------------

:man1:`flux-batch` and :man1:`flux-alloc` do not launch tasks directly, and
therefore job parameters are specified in terms of resource slot size
and number of slots. A resource slot can be thought of as the minimal
resources required for a virtual task. The default slot size is 1 core.

.. include:: common/job-param-batch.rst

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
=========

.. include:: common/job-env-rules.rst

PROCESS RESOURCE LIMITS
=======================

By default these commands propagate some common resource limits (as described
in :linux:man2:`getrlimit`) to the job by setting the ``rlimit`` job shell
option in jobspec.  The set of resource limits propagated can be controlled
via the :option:`--rlimit=RULE` option:

.. include:: common/job-process-resource-limits.rst

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

SHELL OPTIONS
=============

These options are provided by built-in shell plugins that may be
overridden in some cases:

.. include:: common/job-shell-options.rst

RESOURCES
=========

Flux: http://flux-framework.org

SEE ALSO
========

:man1:`flux-run`, :man1:`flux-submit`, :man1:`flux-batch`,
:man1:`flux-bulksubmit`
