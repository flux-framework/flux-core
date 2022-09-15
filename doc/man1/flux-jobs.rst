.. flux-help-include: true

============
flux-jobs(1)
============


SYNOPSIS
========

**flux** **jobs** [*OPTIONS*] [JOBID ...]

DESCRIPTION
===========

flux-jobs(1) is used to list jobs run under Flux. By default only
pending and running jobs for the current user are listed. Additional
jobs and information can be listed using options listed below.
Alternately, specific job ids can be listed on the command line to
only list those job IDs.


OPTIONS
=======

**-a**
   List jobs in all states, including inactive jobs.
   This is shorthand for *--filter=pending,running,inactive*.

**-A**
   List jobs of all users. This is shorthand for *--user=all*.

**-n, --suppress-header**
   For default output, do not output column headers.

**-u, --user**\ *=[USERNAME|UID]*
   List jobs for a specific username or userid. Specify *all* for all users.

**--name**\ *=[JOB NAME]*
   List jobs with a specific job name.

**-c, --count**\ *=N*
   Limit output to N jobs (default 1000)

**--since**\ *WHEN*
   Limit output to jobs that completed or have become inactive since a
   given timestamp. This option implies ``-a`` if no other ``--filter``
   options are specified. If *WHEN* begins with ``-`` character, then
   the remainder is considered to be a an offset in Flux standard duration
   (RFC 23). Otherwise, any datetime expression accepted by the Python
   `parsedatetime <https://github.com/bear/parsedatetime>`_ module is
   accepted. Examples: "-6h", "-1d", "yesterday", "2021-06-21 6am",
   "last Monday", etc. It is assumed to be an error if a timestamp in
   the future is supplied.

**-f, --filter**\ *=STATE|RESULT*
   List jobs with specific job state or result. Multiple states or
   results can be listed separated by comma. See JOB STATUS below for
   additional information. Defaults to *pending,running*.

**-o, --format**\ *=FORMAT*
   Specify output format using Python's string format syntax. See OUTPUT
   FORMAT below for field names.

**--color**\ *=WHEN*
   Control output coloring. WHEN can be *never*, *always*, or *auto*.
   Defaults to *auto*.

**--stats**
   Output a summary of global job statistics before the header.
   May be useful in conjunction with utilities like
   :linux:man1:`watch`, e.g.::

      $ watch -n 2 flux jobs --stats -f running -c 25

   will display a summary of global statistics along with the top 25
   running jobs, updated every 2 seconds.

**--stats-only**
   Output a summary of global job statistics and exit.
   ``flux jobs`` will exit with non-zero exit status with ``--stats-only``
   if there are no active jobs. This allows the following loop to work::

       $ while flux jobs --stats-only; do sleep 2; done

   All other options are ignored when ``--stats-only`` is used.

**-R, --recursive**
   List jobs recursively. Each child job which is also an instance of
   Flux is prefixed by its jobid "path" followed by the list of jobs,
   recursively up to any defined ``-L, --level``. If the ``--stats``
   option is used, then each child instance in the hierarchy is listed
   with its stats.

**--recurse-all**
   By default, jobs not owned by the user running ``flux jobs`` are
   skipped with ``-R, --recursive``, because normally Flux instances
   only permit the instance owner to connect. This option forces the
   command to attempt to recurse into the jobs of other users.  Implies
   ``--recursive``.

**-L, --level**\ *=N*
   With ``-R, --recursive``, stop recursive job listing at level **N**.
   Levels are counted starting at 0, so ``flux jobs -R --level=0`` is
   equivalent to ``flux jobs`` without ``-R``, and ``--level=1`` would
   limit recursive job listing to child jobs of the current instance.

**--threads**\ *=N*
   When ``flux jobs`` recursively queries job lists (with ``--recursive``)
   or fetches info for jobs that are also instances (see
   ``instance.*`` fields), a pool of threads is used to parallelize
   the required RPCs. Normally, the default number of ThreadPoolExecutor
   threads is used, but by using the ``--threads``, a specific number
   of threads can be chosen.


JOB STATUS
==========

Jobs may be observed to pass through five job states in Flux: DEPEND,
SCHED, RUN, CLEANUP, and INACTIVE (see Flux RFC 21). Under the
``state_single`` field name, these are abbreviated as D, S, R, C, and I
respectively. For convenience and clarity, the following virtual job
states also exist: "pending", an alias for DEPEND,SCHED; "running", an
alias for RUN,CLEANUP; "active", an alias for "pending,running".

After a job has finished and is in the INACTIVE state, it can be
marked with one of three possible results: COMPLETED, FAILED,
CANCELED, TIMEOUT. Under the *result_abbrev* field name, these are
abbreviated as CD, F, CA, and TO respectively.

The job status is a user friendly mix of both, a job is always in one
of the following statuses: DEPEND, SCHED, RUN, CLEANUP, COMPLETED,
FAILED, CANCELED, or TIMEOUT. Under the *status_abbrev* field name,
these are abbreviated as D, S, R, C, CD, F, CA, and TO respectively.


OUTPUT FORMAT
=============

The *--format* option can be used to specify an output format to
flux-jobs(1) using Python's string format syntax. For example, the
following is the format used for the default format:

::

   {id.f58:>12} {username:<8.8} {name:<10.10} {status_abbrev:>2.2} {ntasks:>6} {nnodes:>6h} {runtime!F:>8h} {nodelist:h}

The special presentation type *h* can be used to convert an empty
string, "0s", "0.0", or "0:00:00" to a hyphen. For example, normally
"{nodelist}" would output an empty string if the job has not yet run.
By specifying, "{nodelist:h}", a hyphen would be presented instead.

Additionally, the custom job formatter supports a set of special
conversion flags. Conversion flags follow the format field and are
used to transform the value before formatting takes place. Currently,
the following conversion flags are supported by *flux-jobs*:

**!D**
   convert a timestamp field to ISO8601 date and time (e.g. 2020-01-07T13:31:00).
   Defaults to empty string if timestamp field does not exist.

**!d**
   convert a timestamp to a Python datetime object. This allows datetime specific
   format to be used, e.g. *{t_inactive!d:%H:%M:%S}*. However, note that width
   and alignment specifiers are not supported for datetime formatting.
   Defaults to datetime of epoch if timestamp field does not exist.

**!F**
   convert a duration in floating point seconds to Flux Standard Duration (FSD).
   string.  Defaults to empty string if duration field does not exist.

**!H**
   convert a duration to hours:minutes:seconds form (e.g. *{runtime!H}*).
   Defaults to empty string if duration field does not exist.

**!P**
   convert a floating point number into a percentage fitting in 5 characters
   including the "%" character. E.g. 0.5 becomes "50%" 0.015 becomes 1.5%,
   and 0.0005 becomes 0.05% etc.

Annotations can be retrieved via the *annotations* field name.
Specific keys and sub-object keys can be retrieved separated by a
period (".").  For example, if the scheduler has annotated the job
with a reason pending status, it can be retrieved via
"{annotations.sched.reason_pending}".

As a convenience, the field names *sched* and *user* can be used as
substitutions for *annotations.sched* and *annotations.user*.  For
example, a reason pending status can be retrieved via
"{sched.reason_pending}".

As a reminder to the reader, some shells may interpret special
characters in Python's string format syntax.  The format may need to
be quoted or escaped to work under certain shells.

The field names that can be specified are:

**id**
   job ID

**id.f58**
  job ID in RFC 19 F58 (base58) encoding

**id.dec**
  job ID in decimal representation

**id.hex**
   job ID in ``0x`` prefix hexadecimal representation

**id.dothex**
   job ID in dotted hexadecimal representation (``xx.xx.xx.xx``)

**id.words**
  job ID in mnemonic encoding

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

**name**
   job name

**ntasks**
   job task count

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

**result**
   job result if job is inactive (COMPLETED, FAILED, CANCELED, TIMEOUT),
   empty string otherwise

**result_abbrev**
   result but in a max 2 character abbreviation

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


EXAMPLES
========

The default output of flux-jobs(1) will list the pending and running
jobs of the current user.  It is equivalent to:

::

    $ flux jobs --filter=pending,running

To list all pending, running, and inactive jobs, of the current user,
you can use *--filter* option or the *-a* option:

::

    $ flux jobs -a

    OR

    $ flux jobs --filter=pending,running,inactive

To alter which user's jobs are listed, specify the user with *--user*:

::

    $ flux jobs --user=flux

Jobs that have finished may be filtered further by specifying if they
have completed, failed, or were canceled.  For example, the following
will list the jobs that have failed or were canceled:

::

    $ flux jobs --filter=failed,canceled

The *--format* option can be used to alter the output format or output
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

Flux: http://flux-framework.org

SEE ALSO
========

:man1:`flux-pstree`
