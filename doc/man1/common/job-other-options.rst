.. option:: --cwd=DIRECTORY

   Set job working directory.

.. option:: --urgency=N

   Specify job urgency, which affects queue order. Numerically higher urgency
   jobs are considered by the scheduler first. Guests may submit jobs with
   urgency in the range of 0 to 16, while instance owners may submit jobs
   with urgency in the range of 0 to 31 (default 16).  In addition to
   numerical values, the special names ``hold`` (0), ``default`` (16),
   and ``expedite`` (31) are also accepted.

.. option:: -v, --verbose

   *(run,alloc,submit,bulksubmit)* Increase verbosity on stderr. For example,
   currently :option:`flux run -v` displays jobid, :option:`-vv` displays job
   events, and :option:`-vvv` displays exec events. :option:`flux alloc -v`
   forces the command to print the submitted jobid on stderr.  The specific
   output may change in the future.

.. option:: -o, --setopt=KEY[=VAL]

   Set shell option. Keys may include periods to denote hierarchy.
   VAL is optional and may be valid JSON (bare values, objects, or arrays),
   otherwise VAL is interpreted as a string. If VAL is not set, then the
   default value is 1. See `SHELL OPTIONS`_ below.

.. option:: --setattr=KEY[=VAL]

   Set jobspec attribute. Keys may include periods to denote hierarchy.
   If KEY does not begin with ``system.``, ``user.``, or ``.``, then
   ``system.`` is assumed.  VAL is optional and may be valid JSON (bare
   values, objects, or arrays), otherwise VAL is interpreted as a string. If
   VAL is not set, then the default value is 1.  If KEY starts with a ``^``
   character, then VAL is interpreted as a file, which must be valid JSON,
   to use as the attribute value.

.. option:: --add-file=[NAME=]ARG

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

   .. note::
      This option should only be used for small files such as program input
      parameters, configuration, scripts, and so on. For broadcast of large
      files, binaries, and directories, the :man1:`flux-shell` ``stage-in``
      plugin will be more appropriate.

.. option:: --begin-time=+FSD|DATETIME

   Convenience option for setting a ``begin-time`` dependency for a job.
   The job is guaranteed to start after the specified date and time.
   If argument begins with a ``+`` character, then the remainder is
   considered to be an offset in Flux standard duration (RFC 23), otherwise,
   any datetime expression accepted by the Python 
   `parsedatetime <https://github.com/bear/parsedatetime>`_ module
   is accepted, e.g. ``2021-06-21 8am``, ``in an hour``,
   ``tomorrow morning``, etc.

.. option:: --signal=SIG@TIME

   Send signal ``SIG`` to job ``TIME`` before the job time limit. ``SIG``
   can specify either an integer signal number or a full or abbreviated
   signal name, e.g. ``SIGUSR1`` or ``USR1`` or ``10``. ``TIME`` is
   specified in Flux Standard Duration, e.g. ``30`` for 30s or ``1h`` for
   1 hour. Either parameter may be omitted, with defaults of ``SIGUSR1``
   and 60s.  For example, :option:`--signal=USR2` will send ``SIGUSR2`` to
   the job 60 seconds before expiration, and :option:`--signal=@3m` will send
   ``SIGUSR1`` 3 minutes before expiration. Note that if ``TIME`` is
   greater than the remaining time of a job as it starts, the job will
   be signaled immediately.

   The default behavior is to not send any warning signal to jobs.

.. option:: --dry-run

   Don't actually submit job. Just emit jobspec on stdout and exit for
   ``run``, ``submit``, ``alloc``, and ``batch``. For ``bulksubmit``,
   emit a line of output including relevant options for each job which
   would have been submitted,

.. option:: --debug

   Enable job debug events, primarily for debugging Flux itself.
   The specific effects of this option may change in the future.
