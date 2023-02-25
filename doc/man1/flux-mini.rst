============
flux-mini(1)
============


.. warning::

  This command is deprecated.  Please use :man1:`flux-submit`,
  :man1:`flux-run`, :man1:`flux-batch`, :man1:`flux-alloc`, and
  :man1:`flux-bulksubmit` instead.


SYNOPSIS
========

**flux** **mini** **submit** [OPTIONS] [*--ntasks=N*] COMMAND...

**flux** **mini** **bulksubmit** [OPTIONS] [*--ntasks=N*] COMMAND...

**flux** **mini** **run** [OPTIONS] [*--ntasks=N*] COMMAND...

**flux** **mini** **batch** [OPTIONS] *--nslots=N* SCRIPT...

**flux** **mini** **alloc** [OPTIONS] *--nslots=N* [COMMAND...]


DESCRIPTION
===========

flux-mini(1) submits jobs to run under Flux. In the case of **submit**
or **run** the job consists of *N* copies of COMMAND launched together
as a parallel job, while **batch** and **alloc** submit a script or launch
a command as the initial program of a new Flux instance.

If *--ntasks* is unspecified, a value of *N=1* is assumed. Commands that
take *--nslots* have no default and require that *--nslots* or *--nodes*
be specified.

The **submit** and **batch** commands enqueue the job and print its numerical
Job ID on standard output.

The **run** and **alloc** commands do the same interactively, blocking until
the job has completed.

The **bulksubmit** command enqueues one job each for a set of inputs read
on either stdin, or given on the command line. The inputs are optionally
substituted in ``COMMAND`` and/or many submission options. See more in the
:ref:`bulksubmit` section below.

The **flux-mini batch** command submits a script to run as the initial
program of a job running a new instance of Flux. The SCRIPT given on the
command line is assumed to be a file name unless the *--wrap* option is
used, in which case the free arguments are consumed as the script, with
``#!/bin/sh`` as the first line. If no SCRIPT is provided, then one will
be read from standard input. The script may contain submission directives
denoted by ``flux:`` or ``FLUX:`` as described in RFC 36, see
:ref:`submission_directives` below.

For **flux-mini batch**, the SCRIPT given on the command line is assumed
to be a file name, unless the *--wrap* option used, and the script
file is read and submitted along with the job. If no SCRIPT is
provided, then one will be read from *stdin*.

**flux-mini alloc** works similarly to **batch**, but instead blocks until
the job has started and interactively attaches to the new Flux instance
(unless the ``--bg`` option is used).  By default, a new shell is spawned
as the initial program of the instance, but this may be overridden by
supplying COMMAND on the command line.

The intent is for the "mini" commands to remain simple with stable interfaces
over time, making them suitable for use in scripts.

The available OPTIONS are detailed below.

.. note::

  These commands target the *enclosing instance*. For example, within
  a *SCRIPT* submitted with ``flux mini batch``, a ``flux mini`` command
  will submit a job to the batch instance, not the parent instance under
  which the batch instance is running. To target the parent instance,
  the ``--parent`` option of :man1:`flux` should be used, e.g::

   flux --parent mini batch [OPTIONS].. ARGS..


.. include:: common/submit-job-parameters.rst

.. include:: common/submit-standard-io.rst

.. include:: common/submit-constraints.rst

.. include:: common/submit-dependencies.rst

.. include:: common/submit-environment.rst

.. include:: common/submit-process-resource-limits.rst

.. include:: common/submit-exit-status.rst

.. include:: common/submit-other-options.rst

.. _bulksubmit:

BULKSUBMIT
==========

.. include:: common/submit-bulksubmit.rst

.. include:: common/submit-shell-options.rst

.. include:: common/submit-submission-directives.rst


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man1:`flux-submit`, :man1:`flux-run`, :man1:`flux-alloc`, :man1:`flux-batch`,
:man1:`flux-bulksubmit`
