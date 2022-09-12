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


JOB PARAMETERS
==============

These commands accept only the simplest parameters for expressing
the size of the parallel program and the geometry of its task slots:

Common resource options
-----------------------

All subcommands take the following common resource allocation options:

**-N, --nodes=N**
   Set the number of nodes to assign to the job. Tasks will be distributed
   evenly across the allocated nodes, unless the per-resource options
   (noted below) are used with *submit*, *run*, or *bulksubmit*. It is
   an error to request more nodes than there are tasks. If unspecified,
   the number of nodes will be chosen by the scheduler.

**--exclusive**
   Indicate to the scheduler that nodes should be exclusively allocated to
   this job. It is an error to specify this option without also using
   *-N, --nodes*. If *--nodes* is specified without *--nslots* or *--ntasks*,
   then this option will be enabled by default and the number of tasks
   or slots will be set to the number of requested nodes.


Per-task options
----------------

The **run**, **submit** and **bulksubmit** commands take two sets
of mutually exclusive options to specify the size of the job request.
The most common form uses the total number of tasks to run along with
the amount of resources required per task to specify the resources for
the entire job:

**-n, --ntasks=N**
   Set the number of tasks to launch (default 1).

**-c, --cores-per-task=N**
   Set the number of cores to assign to each task (default 1).

**-g, --gpus-per-task=N**
   Set the number of GPU devices to assign to each task (default none).

Per-resource options
--------------------

The second set of options allows an amount of resources to be specified
with the number of tasks per core or node set on the command line. It is
an error to specify any of these options when using any per-task option
listed above:

**--cores=N**
   Set the total number of cores.

**--tasks-per-node=N**
   Set the number of tasks per node to run.

**--gpus-per-node=N**
   With -N, --nodes, request a specific number of GPUs per node.

**--tasks-per-core=N**
   Force a number of tasks per core. Note that this will run *N* tasks per
   *allocated* core. If nodes are exclusively scheduled by configuration or
   use of the ``--exclusive`` flag, then this option could result in many
   more tasks than expected. The default for this option is effectively 1,
   so it is useful only for oversubscribing tasks to cores for testing
   purposes. You probably don't want to use this option.

Batch job options
-----------------

The **batch** and **alloc** commands do not launch tasks directly, and
therefore job parameters are specified in terms of resource slot size
and number of slots. A resource slot can be thought of as the minimal
resources required for a virtual task. The default slot size is 1 core.

**-n, --nslots=N**
   Set the number of slots requested. This parameter is required.

**-c, --cores-per-slot=N**
   Set the number of cores to assign to each slot (default 1).

**-g, --gpus-per-slot=N**
   Set the number of GPU devices to assign to each slot (default none).

Additional job options
----------------------

The **run**, **submit**, **batch**, and **alloc** commands also take
following additional job parameters:

**-t, --time-limit=MINUTES|FSD**
   Set a time limit for the job in either minutes or Flux standard duration
   (RFC 23). FSD is a floating point number with a single character units
   suffix ("s", "m", "h", or "d"). The default unit for the ``--time-limit``
   option is minutes when no units are otherwise specified. If the time
   limit is unspecified, the job is subject to the system default time limit.

STANDARD I/O
============

By default, task stdout and stderr streams are redirected to the
KVS, where they may be accessed with the ``flux job attach`` command.

In addition, ``flux-mini run`` processes standard I/O in real time,
emitting the job's I/O to its stdout and stderr.

**--output=TEMPLATE**
   Specify the filename *TEMPLATE* for stdout redirection, bypassing
   the KVS.  *TEMPLATE* may be a mustache template which supports the
   tags *{{id}}* and *{{jobid}}* which expand to the current jobid
   in the F58 encoding.  If needed, an alternate encoding can be
   selected by using a subkey with the name of the desired encoding,
   e.g. *{{id.dec}}*. Supported encodings include *f58* (the default),
   *dec*, *hex*, *dothex*, and *words*. For **flux mini batch** the
   default *TEMPLATE* is *flux-{{id}}.out*. To force output to KVS so it is
   available with ``flux job attach``, set *TEMPLATE* to *none* or *kvs*.

**--error=TEMPLATE**
   Redirect stderr to the specified filename *TEMPLATE*, bypassing the KVS.
   *TEMPLATE* is expanded as described above.

**-l, --label-io**
   Add task rank prefixes to each line of output.

CONSTRAINTS
===========

.. note::
   Flux supports an advanced constraint specification detailed in RFC 31.
   However, the interface currently exported via the **flux mini** commands
   is purposefully limited.

**--requires=LIST**
   Specify a *LIST* of resource property constraints for this job. *LIST*
   is a single property or comma-separated list of properties which are
   required for this job. The ``--requires`` option may be specified
   multiple times. Currently, all properties are required (logical and).
   If a property name starts with ``^``, then the job requires that property
   *not* be present on assigned resources.

DEPENDENCIES
============

.. note::
   Flux supports a simple but powerful job dependency specification in jobspec.
   See Flux Framework RFC 26 for more detailed information about the generic
   dependency specification.

Dependencies may be specified on the ``flux mini`` command line using the
following option

**--dependency=URI**
   Specify a dependency of the submitted job using RFC 26 dependency URI
   format. The URI format is **SCHEME:VALUE[?key=val[&key=val...]]**.
   The URI will be converted into RFC 26 JSON object form and appended to
   the jobspec ``attributes.system.dependencies`` array. If the current
   Flux instance does not support dependency scheme *SCHEME*, then the
   submitted job will be rejected with an error message indicating this
   fact.

   The ``--dependency`` option may be specified multiple times. Each use
   appends a new dependency object to the ``attributes.system.dependencies``
   array.

The following dependency schemes are built-in:

.. note::
   The ``after*`` dependency schemes listed below all require that the
   target JOBID be currently active or in the job manager's inactive job
   cache. If a target JOBID has been purged by the time the dependent job
   has been submitted, then the submission will be rejected with an error
   that the target job cannot be found.

after:JOBID
   This dependency is satisfied after JOBID starts.

afterany:JOBID
   This dependency is satisfied after JOBID enters the INACTIVE state,
   regardless of the result

afterok:JOBID
   This dependency is satisfied after JOBID enters the INACTIVE state
   with a successful result.

afternotok:JOBID
   This dependency is satisfied after JOBID enters the INACTIVE state
   with an unsuccessful result.

begin-time:TIMESTAMP
   This dependency is satisfied after TIMESTAMP, which is specified in
   floating point seconds since the UNIX epoch. See the ``flux-mini``
   ``--begin-time`` option below for a more user-friendly interface
   to the ``begin-time`` dependency.

In any of the above ``after*`` cases, if it is determined that the
dependency cannot be satisfied (e.g. a job fails due to an exception
with afterok), then a fatal exception of type=dependency is raised
on the current job.

ENVIRONMENT
===========

By default, ``flux-mini`` duplicates the current environment when
submitting jobs. However, a set of environment manipulation options are
provided to give fine control over the requested environment submitted
with the job.

**--env=RULE**
   Control how environment variables are exported with *RULE*. See
   *ENV RULE SYNTAX* section below for more information. Rules are
   applied in the order in which they are used on the command line.
   This option may be specified multiple times.

**--env-remove=PATTERN**
   Remove all environment variables matching *PATTERN* from the current
   generated environment. If *PATTERN* starts with a ``/`` character,
   then it is considered a :linux:man7:`regex`, otherwise *PATTERN* is
   treated as a shell :linux:man7:`glob`. This option is equivalent to
   ``--env=-PATTERN`` and may be used multiple times.

**--env-file=FILE**
   Read a set of environment *RULES* from a *FILE*. This option is
   equivalent to ``--env=^FILE`` and may be used multiple times.

ENV RULES
=========

The ``--env*`` options of ``flux-mini`` allow control of the environment
exported to jobs via a set of *RULE* expressions. The currently supported
rules are

 * If a rule begins with ``-``, then the rest of the rule is a pattern
   which removes matching environment variables. If the pattern starts
   with ``/``, it is a :linux:man7:`regex`, optionally ending with
   ``/``, otherwise the pattern is considered a shell
   :linux:man7:`glob` expression.

   Examples:
      ``-*`` or ``-/.*/`` filter all environment variables creating an
      empty environment.

 * If a rule begins with ``^`` then the rest of the rule is a filename
   from which to read more rules, one per line. The ``~`` character is
   expanded to the user's home directory.

   Examples:
      ``~/envfile`` reads rules from file ``$HOME/envfile``

 * If a rule is of the form ``VAR=VAL``, the variable ``VAR`` is set
   to ``VAL``. Before being set, however, ``VAL`` will undergo simple
   variable substitution using the Python ``string.Template`` class. This
   simple substitution supports the following syntax:

     * ``$$`` is an escape; it is replaced with ``$``
     * ``$var`` will substitute ``var`` from the current environment,
       falling back to the process environment. An error will be thrown
       if environment variable ``var`` is not set.
     * ``${var}`` is equivalent to ``$var``
     * Advanced parameter substitution is not allowed, e.g. ``${var:-foo}``
       will raise an error.

   Examples:
       ``PATH=/bin``, ``PATH=$PATH:/bin``, ``FOO=${BAR}something``

 * Otherwise, the rule is considered a pattern from which to match
   variables from the process environment if they do not exist in
   the generated environment. E.g. ``PATH`` will export ``PATH`` from the
   current environment (if it has not already been set in the generated
   environment), and ``OMP*`` would copy all environment variables that
   start with ``OMP`` and are not already set in the generated environment.
   It is important to note that if the pattern does not match any variables,
   then the rule is a no-op, i.e. an error is *not* generated.

   Examples:
       ``PATH``, ``FLUX_*_PATH``, ``/^OMP.*/``

Since ``flux-mini`` always starts with a copy of the current environment,
the default implicit rule is ``*`` (or ``--env=*``). To start with an
empty environment instead, the ``-*`` rule or ``--env-remove=*`` option
should be used. For example, the following will only export the current
``PATH`` to a job:

::

    flux mini run --env-remove=* --env=PATH ...


Since variables can be expanded from the currently built environment, and
``--env`` options are applied in the order they are used, variables can
be composed on the command line by multiple invocations of ``--env``, e.g.:

::

    flux mini run --env-remove=* \
                  --env=PATH=/bin --env='PATH=$PATH:/usr/bin' ...

Note that care must be taken to quote arguments so that ``$PATH`` is not
expanded by the shell.


This works particularly well when specifying rules in a file:

::

    -*
    OMP*
    FOO=bar
    BAR=${FOO}/baz

The above file would first clear the environment, then copy all variables
starting with ``OMP`` from the current environment, set ``FOO=bar``,
and then set ``BAR=bar/baz``.


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

**pmi.kvs=native**
   Use the native Flux KVS instead of the PMI plugin's built-in key exchange
   algorithm.

**pmi.exchange.k=N**
   Configure the PMI plugin's built-in key exchange algorithm to use a
   virtual tree fanout of ``N`` for key gather/broadcast.  The default is 2.


RESOURCES
=========

Flux: http://flux-framework.org
