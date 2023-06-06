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
   currently ``flux run -v`` displays jobid, ``-vv`` displays job events,
   and ``-vvv`` displays exec events. ``flux alloc -v`` forces the command
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

**--add-file=[NAME=]ARG**
   Add a file to the RFC 37 file archive in jobspec before submission. Both
   the file metadata and content are stored in the archive, so modification
   or deletion of a file after being processed by this option will have no
   effect on the job. If no ``NAME`` is provided, then ``ARG`` is assumed to
   be the path to a local file and the basename of the file will be used as
   ``NAME``.  Otherwise, if ``ARG`` contains a newline, then it is assumed
   to be the raw file data to encode. The file will be extracted by the
   job shell into the job temporary directory and may be referenced as
   ``{{tmpdir}}/NAME`` on the command line, or ``$FLUX_JOB_TMPDIR/NAME``
   in a batch script.  This option may be specified multiple times to
   encode multiple files.  Note: As documented in RFC 14, the file names
   ``script`` and ``conf.json`` are both reserved.

**--conf=FILE|KEY=VAL|STRING**
   The ``--conf`` option allows configuration for a Flux instance started
   via ``flux-batch(1)`` or ``flux-alloc(1)`` to be iteratively built on
   the command line. On first use, a ``conf.json`` entry is added to the
   internal jobspec file archive, and ``-c{{tmpdir}}/conf.json`` is added
   to the flux broker command line. Each subsequent use of the ``--conf``
   option updates this configuration.

   The argument to ``--conf`` may be in one of several forms:

   * A multiline string, e.g. from a batch directive. In this case the string
     is parsed as JSON or TOML::

      # flux: --conf="""
      # flux: [resource]
      # flux: exclude = "0"
      # flux: """

   * A string containing a ``=`` character, in which case the argument is
     parsed as ``KEY=VAL``, where ``VAL`` is parsed as JSON, e.g.::

      --conf=resource.exclude=\"0\"

   * A string ending in ``.json`` or ``.toml``, in which case configuration
     is loaded from a JSON or TOML file.

**--begin-time=+FSD|DATETIME**
   Convenience option for setting a ``begin-time`` dependency for a job.
   The job is guaranteed to start after the specified date and time.
   If argument begins with a ``+`` character, then the remainder is
   considered to be an offset in Flux standard duration (RFC 23), otherwise,
   any datetime expression accepted by the Python 
   `parsedatetime <https://github.com/bear/parsedatetime>`_ module
   is accepted, e.g. ``2021-06-21 8am``, ``in an hour``,
   ``tomorrow morning``, etc.

**--signal=SIG@TIME**
   Send signal ``SIG`` to job ``TIME`` before the job time limit. ``SIG``
   can specify either an integer signal number or a full or abbreviated
   signal name, e.g. ``SIGUSR1`` or ``USR1`` or ``10``. ``TIME`` is
   specified in Flux Standard Duration, e.g. ``30`` for 30s or ``1h`` for
   1 hour. Either parameter may be omitted, with defaults of ``SIGUSR1``
   and 60s.  For example, ``--signal=USR2`` will send ``SIGUSR2`` to
   the job 60 seconds before expiration, and ``--signal=@3m`` will send
   ``SIGUSR1`` 3 minutes before expiration. Note that if ``TIME`` is
   greater than the remaining time of a job as it starts, the job will
   be signaled immediately.

   The default behavior is to not send any warning signal to jobs.

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
   *(submit,bulksubmit)* Log command output and stderr to ``FILE``
   instead of the terminal. If a replacement (e.g. ``{}`` or ``{cc}``)
   appears in ``FILE``, then one or more output files may be opened.
   For example, to save all submitted jobids into separate files, use::

      flux submit --cc=1-4 --log=job{cc}.id hostname

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
   ``exec.shell.init``. For ``flux run`` the argument to this option
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

   $ seq 1 8 | flux bulksubmit --define=pow="2**int(x)" -n {.pow} ...

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
