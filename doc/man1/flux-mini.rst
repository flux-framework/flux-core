.. flux-help-include: true

============
flux-mini(1)
============


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

The ``bulksubmit`` utility allows rapid bulk submission of jobs using
an interface similar to GNU parallel or ``xargs``. The command takes
inputs on stdin or the command line (separated by ``:::``), and submits
the supplied command template and options as one job per input combination.

The replacement is done using Python's ``string.format()``, which is
supplied a list of inputs on each iteration. Therefore, in the common case
of a single input list, ``{}`` will work as the substitution string, e.g.::

    $ seq 1 4 | flux mini bulksubmit echo {}
    flux-mini: submit echo 1
    flux-mini: submit echo 2
    flux-mini: submit echo 3
    flux-mini: submit echo 4

With ``--dry-run`` ``bulksubmit`` will print the args and command which
would have been submitted, but will not perform any job submission.

The ``bulksubmit`` command can also take input lists on the command line.
The inputs are separated from each other and the command  with the special
delimiter ``:::``::

    $ flux mini bulksubmit echo {} ::: 1 2 3 4
    flux-mini: submit echo 1
    flux-mini: submit echo 2
    flux-mini: submit echo 3
    flux-mini: submit echo 4

Multiple inputs are combined, in which case each input is passed as a
positional parameter to the underlying ``format()``, so should be accessed
by index::

    $ flux mini bulksubmit --dry-run echo {1} {0} ::: 1 2 ::: 3 4
    flux-mini: submit echo 3 1
    flux-mini: submit echo 4 1
    flux-mini: submit echo 3 2
    flux-mini: submit echo 4 2

If the generation of all combinations of an  input list with other inputs is not
desired, the special input delimited ``:::+`` may be used to "link" the input,
so that only one argument from this source will be used per other input,
e.g.::

    $ flux mini bulksubmit --dry-run echo {0} {1} ::: 1 2 :::+ 3 4
    flux-mini: submit 1 3
    flux-mini: submit 2 4

The linked input will be cycled through if it is shorter than other inputs.

An input list can be read from a file with ``::::``::

    $ seq 0 3 >inputs
    $ flux mini bulksubmit --dry-run :::: inputs
    flux-mini: submit 0
    flux-mini: submit 1
    flux-mini: submit 2
    flux-mini: submit 3

If the filename is ``-`` then ``stdin`` will be used. This is useful
for including ``stdin`` when reading other inputs.

The delimiter ``::::+`` indicates that the next file is to be linked to
the inputs instead of combined with them, as with ``:::+``.

There are several predefined attributes for input substitution.
These include:

 - ``{.%}`` returns the input string with any extension removed.
 - ``{./}`` returns the basename of the input string.
 - ``{./%}`` returns the basename of the input string with any
   extension removed.
 - ``{.//}`` returns the dirname of the input string
 - ``{seq}`` returns the input sequence number (0 origin)
 - ``{seq1}`` returns the input sequence number (1 origin)
 - ``{cc}`` returns the current ``id`` from use of ``--cc`` or ``--bcc``.
   Note that replacement of ``{cc}`` is done in a second pass, since the
   ``--cc`` option argument may itself be replaced in the first substitution
   pass. If ``--cc/bcc`` were not used, then ``{cc}`` is replaced with an
   empty string. This is the only substitution supported with
   ``flux-mini submit``.

Note that besides ``{seq}``, ``{seq1}``, and ``{cc}`` these attributes
can also take the input index, e.g. ``{0.%}`` or ``{1.//}``, when multiple
inputs are used.

Additional attributes may be defined with the ``--define`` option, e.g.::

    $ flux mini bulksubmit --dry-run --define=p2='2**int(x)' -n {.p2} hostname \
       ::: $(seq 0 4)
    flux-mini: submit -n1 hostname
    flux-mini: submit -n2 hostname
    flux-mini: submit -n4 hostname
    flux-mini: submit -n8 hostname
    flux-mini: submit -n16 hostname

The input string being indexed is passed to defined attributes via the
local ``x`` as seen above.

SHELL OPTIONS
=============

These options are provided by built-in shell plugins that may be
overridden in some cases:

**mpi=spectrum**
   Load the MPI personality plugin for IBM Spectrum MPI. All other MPI
   plugins are loaded by default.

**cpu-affinity=per-task**
   Tasks are distributed across the assigned resources.

**cpu-affinity=off**
   Disable task affinity plugin.

**gpu-affinity=per-task**
   GPU devices are distributed evenly among local tasks. Otherwise,
   GPU device affinity is to the job.

**gpu-affinity=off**
   Disable GPU affinity for this job.

**verbose**
   Increase verbosity of the job shell log.

**nosetpgrp**
   Normally the job shell runs each task in its own process group to
   facilitate delivering signals to tasks which may call :linux:man2:`fork`.
   With this option, the shell avoids calling :linux:man2:`setpgrp`, and
   each task will run in the process group of the shell. This will cause
   signals to be delivered only to direct children of the shell.

**pmi=off**
   Disable the process management interface (PMI-1) which is required for
   bootstrapping most parallel program environments.  See :man1:`flux-shell`
   for more pmi options.

**stage-in**
   Copy files previously mapped with :man1:`flux-filemap` to $FLUX_JOB_TMPDIR.
   See :man1:`flux-shell` for more *stage-in* options.

.. _submission_directives:

SUBMISSION DIRECTIVES
=====================

The *flux mini batch* command supports submission directives
mixed within the submission script. The submission directive specification
is fully detailed in RFC 36, but is summarized here for convenience:

 * A submission directive is indicated by a line that starts with
   a prefix of non-alphanumeric characters followed by a tag ``FLUX:`` or
   ``flux:``. The prefix plus tag is called the *directive sentinel*. E.g.,
   in the example below the sentinel is ``# flux:``: ::

     #!/bin/sh
     # flux: -N4 -n16
     flux mini run -n16 hostname

 * All directives in a file must use the same sentinel pattern, otherwise
   an error will be raised.
 * Directives must be grouped together - it is an error to include a
   directive after any non-blank line that doesn't start with the common
   prefix.
 * The directive starts after the sentinel to the end of the line.
 * The ``#`` character is supported as a comment character in directives.
 * UNIX shell quoting is supported in directives.
 * Triple quoted strings can be used to include newlines and quotes without
   further escaping. If a triple quoted string is used across multiple lines,
   then the opening and closing triple quotes must appear at the end of the
   line. For example ::

     # flux: --setattr=user.conf="""
     # flux: [config]
     # flux:   item = "foo"
     # flux: """

Submission directives may be used to set default command line options for
*flux mini batch* for a given script. Options given on the *flux mini batch*
command line override those in the submission script, e.g.: ::

   $ flux mini batch --job-name=test-name --wrap <<-EOF
   > #flux: -N4
   > #flux: --job-name=name
   > flux mini run -N4 hostname
   > EOF
   ƒ112345
   $ flux jobs -no {name} ƒ112345
   test-name


RESOURCES
=========

Flux: http://flux-framework.org
