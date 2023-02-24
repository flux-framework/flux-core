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

PROCESS RESOURCE LIMITS
=======================

By default flux mini propagates some common resource limits (as described
in :linux:man2:`getrlimit`) to the job by setting the ``rlimit`` job shell
option in jobspec.  The set of resource limits propagated can be controlled
via the ``--rlimit=RULE`` option:

**--rlimit=RULE**
    Control how process resource limits are propagated with *RULE*. Rules
    are applied in the order in which they are used on the command line.
    This option may be used multiple times.

The ``--rlimit`` rules work similar to the ``--env`` option rules:

 * If a rule begins with ``-``, then the rest of the rule is a name or
   :linux:man7:`glob` pattern which removes matching resource limits from
   the set to propagate.

   Example:
     ``-*`` disables propagation of all resource limits.

 * If a rule is of the form ``LIMIT=VALUE`` then *LIMIT* is explicitly
   set to *VALUE*. If *VALUE* is ``unlimited``, ``infinity`` or ``inf``,
   then the value is set to ``RLIM_INFINITY`` (no limit).

   Example:
     ``nofile=1024`` overrides the current ``RLIMIT_NOFILE`` limit to 1024.

 * Otherwise, *RULE* is considered a pattern from which to match resource
   limits and propagate the current limit to the job, e.g.

      ``--rlimit=memlock``

   will propagate ``RLIMIT_MEMLOCK`` (which is not in the list of limits
   that are propagated by default).

``flux-mini`` starts with a default list of resource limits to propagate,
then applies all rules specified via ``--rlimit`` on the command line.
Therefore, to propagate only one limit, ``-*`` should first be used to
start with an empty set, e.g. ``--rlimit=-*,core`` will only propagate the
``core`` resource limit.

The set of resource limits propagated by default includes all those except
``memlock``, ``ofile``, ``msgqueue``, ``nice``, ``rtprio``, ``rttime``,
and ``sigpending``. To propagate all possible resource limits, use
``--rlimit=*``.


EXIT STATUS
===========

The job exit status, normally the largest task exit status, is stored
in the KVS. If one or more tasks are terminated with a signal,
the job exit status is 128+signo.

The ``flux-job attach`` command exits with the job exit status.

In addition, ``flux-mini run`` runs until the job completes and exits
with the job exit status.


OTHER OPTIONS
=============

**--cwd=DIRECTORY**
   Set job working directory.

**--urgency=N**
   Specify job urgency, which affects queue order. Numerically higher urgency
   jobs are considered by the scheduler first. Guests may submit jobs with
   urgency in the range of 0 to 16, while instance owners may submit jobs
   with urgency in the range of 0 to 31 (default 16).  In addition to
   numerical values, the special names ``hold`` (0), ``default`` (16),
   and ``expedite`` (31) are also accepted.

**-v, --verbose**
   *(run,alloc,submit,bulksubmit)* Increase verbosity on stderr. For example,
   currently ``flux mini run -v`` displays jobid, ``-vv`` displays job events,
   and ``-vvv`` displays exec events. ``flux mini alloc -v`` forces the command
   to print the submitted jobid on stderr.
   The specific output may change in the future.

**-o, --setopt=KEY[=VAL]**
   Set shell option. Keys may include periods to denote hierarchy.
   VAL is optional and may be valid JSON (bare values, objects, or arrays),
   otherwise VAL is interpreted as a string. If VAL is not set, then the
   default value is 1. See SHELL OPTIONS below.

**--setattr=KEY[=VAL]**
   Set jobspec attribute. Keys may include periods to denote hierarchy.
   If KEY does not begin with ``system.``, ``user.``, or ``.``, then
   ``system.`` is assumed.  VAL is optional and may be valid JSON (bare
   values, objects, or arrays), otherwise VAL is interpreted as a string. If
   VAL is not set, then the default value is 1.  If KEY starts with a ``^``
   character, then VAL is interpreted as a file, which must be valid JSON,
   to use as the attribute value.

**--begin-time=DATETIME**
   Convenience option for setting a ``begin-time`` dependency for a job.
   The job is guaranteed to start after the specified date and time.
   If *DATETIME* begins with a ``+`` character, then the remainder is
   considered to be an offset in Flux standard duration (RFC 23), otherwise,
   any datetime expression accepted by the Python 
   `parsedatetime <https://github.com/bear/parsedatetime>`_ module
   is accepted, e.g. ``2021-06-21 8am``, ``in an hour``,
   ``tomorrow morning``, etc.

**--taskmap=SCHEME[:VALUE]**
   Choose an alternate method for mapping job task IDs to nodes of the
   job. The job shell maps tasks using a "block" distribution scheme by
   default (consecutive tasks share nodes) This option allows the
   activation of alternate schemes by name, including an optional *VALUE*.
   Supported schemes which are built in to the job shell include

   cyclic[:N]
    Tasks are distributed over consecutive nodes with a stride of *N*
    (where N=1 by default).

   manual:TASKMAP
    An explicit RFC 34 taskmap is provided and used to manually map
    task ids to nodes. The provided *TASKMAP* must match the total number
    of tasks in the job and the number of tasks per node assigned by
    the job shell, so this option is not useful unless the total number
    of nodes and tasks per node are known at job submission time.

   However, shell plugins may provide other task mapping schemes, so
   check the current job shell configuration for a full list of supported
   taskmap schemes.

**--dry-run**
   Don't actually submit job. Just emit jobspec on stdout and exit for
   ``run``, ``submit``, ``alloc``, and ``batch``. For ``bulksubmit``,
   emit a line of output including relevant options for each job which
   would have been submitted,

**--debug**
   Enable job debug events, primarily for debugging Flux itself.
   The specific effects of this option may change in the future.

**--bg**
   *(alloc only)* Do not interactively attach to the instance. Instead,
   print jobid on stdout once the instance is ready to accept jobs. The
   instance will run indefinitely until a time limit is reached, the
   job is canceled, or it is shutdown with ``flux shutdown JOBID``
   (preferred). If a COMMAND is given then the job will run until COMMAND
   completes. Note that ``flux job attach JOBID`` cannot be used to
   interactively attach to the job (though it will print any errors or
   output).

**-B, --broker-opts=OPT**
   *(batch only)* For batch jobs, pass specified options to the Flux brokers
   of the new instance. This option may be specified multiple times.

**--wrap**
   *(batch only)* The ``--wrap`` option wraps the specified COMMAND and ARGS in
   a shell script, by prefixing with ``#!/bin/sh``. If no COMMAND is present,
   then a SCRIPT is read on stdin and wrapped in a /bin/sh script.

**--cc=IDSET**
   *(submit,bulksubmit)* Replicate the job for each ``id`` in ``IDSET``.
   ``FLUX_JOB_CC=id`` will be set in the environment of each submitted job
   to allow the job to alter its execution based on the submission index.
   (e.g. for reading from a different input file). When using ``--cc``,
   the substitution string ``{cc}`` may be used in options and commands
   and will be replaced by the current ``id``.

**--bcc=IDSET**
   *(submit,bulksubmit)* Identical to ``--cc``, but do not set
   ``FLUX_JOB_CC`` in each job. All jobs will be identical copies.
   As with ``--cc``, ``{cc}`` in option arguments and commands will be
   replaced with the current ``id``.

**--quiet**
   *(submit,bulksubmit)* Suppress logging of jobids to stdout.

**--log=FILE**
   *(submit,bulksubmit)* Log ``flux-mini`` output and stderr to ``FILE``
   instead of the terminal. If a replacement (e.g. ``{}`` or ``{cc}``)
   appears in ``FILE``, then one or more output files may be opened.
   For example, to save all submitted jobids into separate files, use::

      flux mini submit --cc=1-4 --log=job{cc}.id hostname

**--log-stderr=FILE**
   *(submit,bulksubmit)* Separate stderr into ``FILE`` instead of sending
   it to the terminal or a ``FILE`` specified by ``--log``.

**--wait**
   *(submit,bulksubmit)* Wait on completion of all jobs before exiting.
   This is equivalent to ``--wait-event=clean``.

**--wait-event=NAME**
   *(run,submit,bulksubmit)* Wait until job or jobs have received event ``NAME``
   before exiting. E.g. to submit a job and block until the job begins
   running, use ``--wait-event=start``. *(submit,bulksubmit only)* If ``NAME``
   begins with ``exec.``, then wait for an event in the exec eventlog, e.g.
   ``exec.shell.init``. For ``flux mini run`` the argument to this option
   when used is passed directly to ``flux job attach``.

**--watch**
   *(submit,bulksubmit)* Display output from all jobs. Implies ``--wait``.

**--progress**
   *(submit,bulksubmit)* With ``--wait``, display a progress bar showing
   the progress of job completion. Without ``--wait``, the progress bar
   will show progress of job submission.

**--jps**
   *(submit,bulksubmit)* With ``--progress``, display throughput statistics
   (jobs/s) in the progress bar.

**--define=NAME=CODE**
   *(bulksubmit)* Define a named method that will be made available as an
   attribute during command and option replacement. The string being
   processed is available as ``x``. For example::

   $ seq 1 8 | flux mini bulksubmit --define=pow="2**int(x)" -n {.pow} ...

**--shuffle**
   *(bulksubmit)* Shuffle the list of commands before submission.

**--sep=STRING**
   *(bulksubmit)* Change the separator for file input. The default is
   to separate files (including stdin) by newline. To separate by
   consecutive whitespace, specify ``--sep=none``.

**--dump=[FILE]**
   *(batch,alloc)* When the job script is complete, archive the Flux
   instance's KVS content to ``FILE``, which should have a suffix known
   to :linux:man3:`libarchive`, and may be a mustache template as described
   above for ``--output``.  The content may be unarchived directly or examined
   within a test instance started with the :man1:`flux-start` ``--recovery``
   option.  If ``FILE`` is unspecified, ``flux-{{jobid}}-dump.tgz`` is used.

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
