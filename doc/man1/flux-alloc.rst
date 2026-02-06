.. flux-help-include: true
.. flux-help-section: submission

=============
flux-alloc(1)
=============

SYNOPSIS
========

**flux** **alloc** [OPTIONS] [COMMAND...]

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
   $ flux run -n16 ./testprog
   ...
   $ flux shutdown
   ...
   $

The available OPTIONS are detailed below.

JOB PARAMETERS
==============

:program:`flux batch` and :program:`flux alloc` do not launch tasks directly,
and therefore job parameters are normally specified in terms of resource slot
size and number of slots. A resource slot can be thought of as the minimal
resources required for a virtual task. The default slot size is 1 core.

.. include:: common/job-param-batch.rst

.. include:: common/job-param-additional.rst

CONSTRAINTS
===========

.. include:: common/job-constraints.rst

DEPENDENCIES
============

.. include:: common/job-dependencies.rst

ENVIRONMENT
===========

By default, :man1:`flux-alloc` duplicates the current environment when
submitting jobs. However, a set of environment manipulation options are
provided to give fine control over the requested environment submitted
with the job.

.. note::
   The actual environment of the initial program is subject to the caveats
   described in the :ref:`initial_program_environment` section of
   :man7:`flux-environment`.

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

EXIT STATUS
===========

The job exit status is normally the batch script exit status.
This result is stored in the KVS.

OTHER OPTIONS
=============

.. include:: common/job-other-options.rst

.. include:: common/job-other-batch.rst

SHELL OPTIONS
=============

Some options that affect the parallel runtime environment of the Flux
instance started by :program:`flux alloc` are provided by the Flux shell.
These options are described in detail in :man7:`flux-shell-options`.
A list of the most commonly needed options follows.

Usage: :option:`flux alloc -o NAME[=ARG]`.

.. make :option: references in the included table x-ref to flux-shell-options(7)
.. program:: flux shell options
.. include:: common/job-shell-options.rst
.. program:: flux alloc

RESOURCES
=========

.. include:: common/resources.rst

SEE ALSO
========

:man1:`flux-run`, :man1:`flux-submit`, :man1:`flux-batch`,
:man1:`flux-bulksubmit`, :man7:`flux-environment`
