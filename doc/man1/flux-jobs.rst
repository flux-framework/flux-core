.. flux-help-section: jobs

============
flux-jobs(1)
============


SYNOPSIS
========

**flux** **jobs** [*OPTIONS*] [JOBID ...]

DESCRIPTION
===========

.. program:: flux jobs

:program:`flux jobs` is used to list jobs run under Flux. By default only
pending and running jobs for the current user are listed. Additional
jobs and information can be listed using options listed below.
Alternately, specific job ids can be listed on the command line to
only list those job IDs.


OPTIONS
=======

.. option:: -a

   List jobs in all states, including inactive jobs.
   This is shorthand for :option:`--filter=pending,running,inactive`.

.. option:: -A

   List jobs of all users. This is shorthand for :option:`--user=all`.

.. option:: -n, --no-header

   For default output, do not output column headers.

.. option:: -u, --user=[USERNAME|UID]

   List jobs for a specific username or userid. Specify *all* for all users.

.. option:: --name=[JOB NAME]

   List jobs with a specific job name.

.. option:: --queue=[QUEUE]

   List jobs in a specific queue.

.. option:: -c, --count=N

   Limit output to N jobs (default 1000)

.. option:: --since=WHEN

   Limit output to jobs that have been active since a given timestamp.  In other
   words, jobs that are currently pending, currently running, or became inactive
   since the given timestamp.  This option implies :option:`-a` if no other
   :option:`--filter` options are specified.  If *WHEN* begins with ``-``
   character, then the remainder is considered to be a an offset in Flux
   standard duration (RFC 23). Otherwise, any datetime expression accepted by
   the Python `parsedatetime <https://github.com/bear/parsedatetime>`_ module
   is accepted. Examples: "-6h", "-1d", "yesterday", "2021-06-21 6am",
   "last Monday", etc. It is assumed to be an error if a timestamp in
   the future is supplied.

.. option:: -f, --filter=STATE|RESULT

   List jobs with specific job state or result. Multiple states or
   results can be listed separated by comma. See `JOB STATUS`_ below for
   additional information. Defaults to *pending,running*.

.. option:: -o, --format=NAME|FORMAT

   Specify a named output format *NAME* or a format string using Python's
   format syntax. See `OUTPUT FORMAT`_ below for field names. Named formats
   may be listed via :option:`--format=help`.  An alternate default format can
   be set via the :envvar:`FLUX_JOBS_FORMAT_DEFAULT` environment variable.
   Additional named formats may be registered with :program:`flux jobs` via
   configuration.  See the `CONFIGURATION`_ section for more details. A
   configuration snippet for an existing named format may be generated with
   :option:`--format=get-config=NAME`.

.. option:: --json

   Emit data for selected jobs in JSON format. The data for multiple
   matching jobs is contained in a ``jobs`` array in the emitted JSON
   object, unless a single job was selected by jobid on the command
   line, in which case a JSON object representing that job is emitted on
   success. With :option:`--recursive`, each job which is also an instance
   of Flux will will have any recursively listed jobs in a ``jobs`` array,
   and so on for each sub-child.

   Only the attributes which are available at the time of the
   :program:`flux jobs` query will be present in the returned JSON object for
   a job. For instance a pending job will not have ``runtime``, ``waitstatus``
   or ``result`` keys, among others. A missing key should be considered
   unavailable.

   The :option:`--json` option is incompatible with :option:`--stats` and
   :option:`--stats-only`, and any :option:`--format` is ignored.

.. option:: --color[=WHEN]

   Control output coloring.  The optional argument *WHEN* can be
   *auto*, *never*, or *always*.  If *WHEN* is omitted, it defaults to
   *always*.  Otherwise the default is *auto*.

.. option:: --stats

   Output a summary of job statistics before the header.  By default
   shows global statistics.  If :option:`--queue` is specified, shows
   statistics for the specified queue.  May be useful in conjunction
   with utilities like :linux:man1:`watch`, e.g.::

      $ watch -n 2 flux jobs --stats -f running -c 25

   will display a summary of statistics along with the top 25
   running jobs, updated every 2 seconds.

   Note that all job failures, including canceled and timeout jobs,
   are collectively counted as "failed" in :option:`--stats`.

.. option:: --stats-only

   Output a summary of job statistics and exit.  By default shows
   global statistics.  If :option:`--queue` is specified, shows statistics
   for the specified queue.  :program:`flux jobs` will exit with non-zero
   exit status with :option:`--stats-only` if there are no active jobs. This
   allows the following loop to work::

       $ while flux jobs --stats-only; do sleep 2; done

   All options other than :option:`--queue` are ignored when
   :option:`--stats-only` is used.

   Note that all job failures, including canceled and timeout jobs,
   are collectively counted as "failed" in :option:`--stats-only`.

.. option:: -R, --recursive

   List jobs recursively. Each child job which is also an instance of
   Flux is prefixed by its jobid "path" followed by the list of jobs,
   recursively up to any defined :option:`--level`. If the :option:`--stats`
   option is used, then each child instance in the hierarchy is listed
   with its stats.

.. option:: --recurse-all

   By default, jobs not owned by the user running :program:`flux jobs` are
   skipped with :option:`--recursive`, because normally Flux instances
   only permit the instance owner to connect. This option forces the
   command to attempt to recurse into the jobs of other users.  Implies
   :option:`--recursive`.

.. option:: -L, --level=N

   With :option:`--recursive`, stop recursive job listing at level **N**.
   Levels are counted starting at 0, so :option:`flux jobs -R --level=0` is
   equivalent to :program:`flux jobs` without :option:`-R`, and
   :option:`--level=1` would limit recursive job listing to child jobs of the
   current instance.

.. option:: --threads=N

   When :program:`flux jobs` recursively queries job lists (with
   :option:`--recursive`) or fetches info for jobs that are also instances
   (see ``instance.*`` fields), a pool of threads is used to parallelize
   the required RPCs. Normally, the default number of ThreadPoolExecutor
   threads is used, but by using the :option:`--threads`, a specific number
   of threads can be chosen.

.. _flux_jobs_job_status:

JOB STATUS
==========

Jobs may be observed to pass through five job states in Flux: DEPEND,
SCHED, RUN, CLEANUP, and INACTIVE (see Flux RFC 21). Under the
*state_single* field name, these are abbreviated as D, S, R, C, and I
respectively. For convenience and clarity, the following virtual job
states also exist: "pending", an alias for DEPEND,SCHED; "running", an
alias for RUN,CLEANUP; "active", an alias for "pending,running".

After a job has finished and is in the INACTIVE state, it can be
marked with one of the possible results: COMPLETED, FAILED,
CANCELED, TIMEOUT. Under the *result_abbrev* field name, these are
abbreviated as CD, F, CA, and TO respectively.

The job status is a user friendly mix of both, a job is always in one
of the following statuses: DEPEND, SCHED, RUN, CLEANUP, COMPLETED,
FAILED, CANCELED, or TIMEOUT. Under the *status_abbrev* field name,
these are abbreviated as D, S, R, C, CD, F, CA, and TO respectively.


.. _flux_jobs_output_format:

OUTPUT FORMAT
=============

The :option:`--format` option can be used to specify an output format to
:program:`flux jobs` using Python's string format syntax. For example, the
following is the format used for the default format:

::

   {id.f58:>12} ?:{queue:<8.8} {username:<8.8} {name:<10.10+} \
   {status_abbrev:>2.2} {ntasks:>6} {nnodes:>6h} \
   {contextual_time!F:>8h} {contextual_info}

If a format field is preceded by the special string ``?:`` this will
cause the field to be removed entirely from output if the result would
be an empty string or zero value for all jobs in the listing. E.g.::

   {id.f58:>12} ?:{exception.type}

would eliminate the EXCEPTION-TYPE column if no jobs in the list received
an exception. (Thus the job queue is only displayed if at least one job
has a queue assigned in the default format shown above).

As a reminder to the reader, some shells will interpret braces
(``{`` and ``}``) in the format string.  They may need to be quoted.

The special presentation type *h* can be used to convert an empty
string, "0s", "0.0", "0:00:00", or epoch time to a hyphen. For example, normally
"{nodelist}" would output an empty string if the job has not yet run.
By specifying, "{nodelist:h}", a hyphen would be presented instead.

The special suffix *+* can be used to indicate if a string was truncated
by including a ``+`` character when truncation occurs. If both *h* and
*+* are being used, then the *+* must appear after the *h*.

Additionally, the custom job formatter supports a set of special
conversion flags. Conversion flags follow the format field and are
used to transform the value before formatting takes place. Currently,
the following conversion flags are supported by :program:`flux jobs`:

**!D**
   convert a timestamp field to ISO8601 date and time (e.g. 2020-01-07T13:31:00).
   Defaults to empty string if timestamp field does not exist or the timestamp
   is 0 (i.e epoch time).

**!d**
   convert a timestamp to a Python datetime object. This allows datetime
   specific format to be used, e.g. *{t_inactive!d:%H:%M:%S}*. Additionally,
   width and alignment can be specified after the time format by using
   two colons (``::``), e.g. *{t_inactive!d:%H:%M:%S::>20}*. Returns an
   empty string (or "-" if the *h* suffix is used) for an unset timestamp.

**!F**
   convert a time duration in floating point seconds to Flux Standard
   Duration (FSD) string (e.g. *{runtime!F}*).  Defaults to empty string if
   field does not exist.

**!H**
   convert a time duration in floating point seconds to
   hours:minutes:seconds form (e.g. *{runtime!H}*).  Defaults to empty
   string if time duration field does not exist.

**!P**
   convert a floating point number into a percentage fitting in 5 characters
   including the "%" character. E.g. 0.5 becomes "50%" 0.015 becomes 1.5%,
   and 0.0005 becomes 0.05% etc.

As a reminder to the reader, some shells will interpret the exclamation
point (``!``) when using a conversion flag.  The exclamation point may
need to be escaped (``\!``).

Annotations can be retrieved via the *annotations* field name.
Specific keys and sub-object keys can be retrieved separated by a
period (".").  For example, if the scheduler has annotated the job
with a reason pending status, it can be retrieved via
"{annotations.sched.reason_pending}".

As a convenience, the field names *sched* and *user* can be used as
substitutions for *annotations.sched* and *annotations.user*.  For
example, a reason pending status can be retrieved via
"{sched.reason_pending}".

The field names that can be specified are:

**id**
   job ID

**id.f58**
  job ID in RFC 19 F58 (base58) encoding

**id.f58plain**
  job ID in RFC 19 F58 encoding with ascii ``f``

**id.dec**
  job ID in decimal representation

**id.hex**
   job ID in ``0x`` prefix hexadecimal representation

**id.dothex**
   job ID in dotted hexadecimal representation (``xx.xx.xx.xx``)

**id.words**
  job ID in mnemonic encoding

**id.emoji**
  job ID in emoji encoding

**userid**
   job submitter's userid

**username**
   job submitter's username

**urgency**
   job urgency

**priority**
   job priority

**dependencies**
   list of any currently outstanding job dependencies

**status**
   job status (DEPEND, SCHED, RUN, CLEANUP, COMPLETED, FAILED,
   CANCELED, or TIMEOUT)

**status_abbrev**
   status but in a max 2 character abbreviation

**status_abbrev**
   status but an appropriate emoji instead of job state / result

**name**
   job name

**cwd**
   job current working directory

**queue**
   job queue

**project**
   job accounting project

**bank**
   job accounting bank

**ntasks**
   job task count

**ncores**
   job core count

**duration**
   job duration in seconds

**nnodes**
   job node count (if job ran / is running), empty string otherwise

**ranks**
   job ranks (if job ran / is running), empty string otherwise

**nodelist**
   job nodelist (if job ran / is running), empty string otherwise

**state**
   job state (DEPEND, SCHED, RUN, CLEANUP, INACTIVE)

**state_single**
   job state as a single character

**state_emoji**
   job state but an appropriate emoji instead of DEPEND, SCHED, RUN,
   CLEANUP, or INACTIVE

**result**
   job result if job is inactive (COMPLETED, FAILED, CANCELED, TIMEOUT),
   empty string otherwise

**result_abbrev**
   result but in a max 2 character abbreviation

**result_emoji**
   result but an appropriate emoji instead of COMPLETED, FAILED,
   CANCELED, or TIMEOUT

**success**
   True of False if job completed successfully, empty string otherwise

**waitstatus**
   The raw status of the job as returned by :linux:man2:`waitpid` if the job
   exited, otherwise an empty string. Note: *waitstatus* is the maximum
   wait status returned by all job shells in a job, which may not necessarily
   indicate the highest *task* wait status. (The job shell exits with the
   maximum task exit status, unless a task died due to a signal, in which
   case the shell exits with 128+signo)

**returncode**
   The job return code if the job has exited, or an empty string if the
   job is still active. The return code of a job is the highest job shell
   exit code, or negative signal number if the job shell was terminated by
   a signal. If the job was canceled before it started, then the returncode
   is set to the special value -128.

**exception.occurred**
   True of False if job had an exception, empty string otherwise

**exception.severity**
   If exception.occurred True, the highest severity, empty string otherwise

**exception.type**
   If exception.occurred True, the highest severity exception type, empty string otherwise

**exception.note**
   If exception.occurred True, the highest severity exception note, empty string otherwise

**t_submit**
   time job was submitted

**t_depend**
   time job entered depend state

**t_run**
   time job entered run state

**t_cleanup**
   time job entered cleanup state

**t_inactive**
   time job entered inactive state

**runtime**
   job runtime

**expiration**
   time at which job allocation was marked to expire

**t_remaining**
   If job is running, amount of time remaining before expiration

**annotations**
   annotations metadata, use "." to get specific keys

**sched**
   short hand for *annotations.sched*

**user**
   short hand for *annotations.user*


Field names which are specific to jobs which are also instances of Flux
include:

**instance.stats**
   a short string describing current job statistics for the instance of
   the form ``PD:{pending} R:{running} CD:{successful} F:{failed}``

**instance.stats.total**
   total number of jobs in any state in the instance.

**instance.utilization**
   number of cores currently allocated divided by the total number of cores.
   Can be formatted as a percentage with ``!P``, e.g.
   ``{instance.utilization!P:>4}``.

**instance.gpu_utilization**
   same as ``instance.utilization`` but for gpu resources

**instance.progress**
   number of inactive jobs divided by the total number of jobs.
   Can be formatted as a percentage with ``{instance.progress!P:>4}``

**instance.resources.<state>.{ncores,ngpus}**
   number of cores, gpus in state ``state``, where ``state`` can be
   ``all``, ``up``, ``down``, ``allocated``, or ``free``, e.g.
   ``{instance.resources.all.ncores}``

The following fields may return different information depending on
the state of the job or other context:

**contextual_info**
   Returns selected information based on the job's current state.  If the
   job is in PRIORITY state, then the string ``priority-wait`` is returned,
   if the job is in DEPEND state, then a list of outstanding  dependencies
   is returned, if the job is in SCHED state then an estimated time the
   job will run is returned (if the scheduler supports it). Otherwise,
   the assigned nodelist is returned (if resources were assigned).

**contextual_info**
   Returns the job runtime for jobs in RUN state or later, otherwise the
   job duration (if set) is returned.

**inactive_reason**
   If the job is inactive, returns the reason that the job is no
   longer active.  Generally speaking, will output "Exit", "Timeout",
   "Canceled", or signal.  If available, other contextual information
   will also be provided such as the exit ``returncode`` or
   cancellation message.

.. _flux_jobs_configuration:

CONFIGURATION
=============

The :program:`flux jobs` command supports registration of named output formats
in configuration files. The command loads configuration files from
``flux-jobs.EXT`` from the following paths in order of increasing precedence:

 * ``$XDG_CONFIG_DIRS/flux`` or ``/etc/xdg/flux`` if :envvar:`XDG_CONFIG_DIRS`
   is not set. Note that :envvar:`XDG_CONFIG_DIRS` is traversed in reverse
   order such that entries first in the colon separated path are highest
   priority.

 * ``$XDG_CONFIG_HOME/flux`` or ``$HOME/.config/flux`` if
   :envvar:`XDG_CONFIG_HOME` is not set

where ``EXT`` can be one of ``toml``, ``yaml``, or ``json``.

If there are multiple ``flux-jobs.*`` files found in a directory, then
they are loaded in lexical order (i.e. ``.json`` first, then ``.toml``,
then ``.yaml``)

Named formats are registered in a ``formats`` table or dictionary with a
key per format pointing to a table or dictionary with the keys:

**format**
   (required) The format string

**description**
   (optional) A short description of the named format, displayed with
   :option:`flux jobs --format=help`

If a format name is specified in more than one config file, then the last
one loaded is used. Due to the order that :program:`flux jobs` loads config
files, this allows user configuration to override system configuration. It is
an error to override any internally defined formats (such as ``default``).

If a format name or string is not specified on the command line the
internally defined format ``default`` is used.

Example::

  # $HOME/.config/flux/flux-jobs.toml

  [formats.myformat]
  description = "My useful format"
  format = """\
  {id.f58:>12} {name:>8.8} {t_submit!D:<19} \
  {t_run!D:<19} {t_remaining!F}\
  """

It may be helpful to start with an existing named format by using the
:option:`--format=get-config=NAME` option, e.g.::

  $ flux jobs --format=get-config=default >> ~/.config/flux/flux-jobs.toml

Be sure to change the name of the format string from ``default``. It is an
error to redefine the default format string.


EXAMPLES
========

The default output of :program:`flux jobs` will list the pending and running
jobs of the current user.  It is equivalent to:

::

    $ flux jobs --filter=pending,running

To list all pending, running, and inactive jobs, of the current user,
you can use :option:`--filter` option or the :option:`-a` option:

::

    $ flux jobs -a

    OR

    $ flux jobs --filter=pending,running,inactive

To alter which user's jobs are listed, specify the user with :option:`--user`:

::

    $ flux jobs --user=flux

Jobs that have finished may be filtered further by specifying if they
have completed, failed, or were canceled.  For example, the following
will list the jobs that have failed or were canceled:

::

    $ flux jobs --filter=failed,canceled

The :option:`--format` option can be used to alter the output format or output
additional information.  For example, the following would output all
jobids for the user in decimal form, and output any annotations the
scheduler attached to each job:

::

   $ flux jobs -a --format="{id} {annotations.sched}"

The following would output the job id and exception information, so a
user can learn why a job failed.

::

   $ flux jobs --filter=failed --format="{id} {exception.type} {exception.note}"



RESOURCES
=========

.. include:: common/resources.rst

SEE ALSO
========

:man1:`flux-pstree`
