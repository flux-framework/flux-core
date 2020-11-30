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
   List all jobs of the current user, including inactive jobs.
   Equivalent to specifying *--filter=pending,running,inactive*.

**-A**
   List all jobs from all users, including inactive jobs. Equivalent to
   specifying *--filter=pending,running,inactive --user=all*.

**-n, --suppress-header**
   For default output, do not output column headers.

**-u, --user**\ *=[USERNAME|UID]*
   List jobs for a specific username or userid. Specify *all* for all users.

**-c, --count**\ *=N*
   Limit output to N jobs (default 1000)

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
CANCELLED. Under the *result_abbrev* field name, these are
abbreviated as CD, F, and CA respectively.

The job status is a user friendly mix of both, a job is always in one
of the following five statuses: PENDING, RUNNING, COMPLETED, FAILED,
or CANCELLED. Under the *status_abbrev* field name, these are
abbreviated as P, R, CD, F, and CA respectively.


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

**admin_priority**
   job administrative priority

**queue_priority**
   job queue priority

**status**
   job status (PENDING, RUNNING, COMPLETED, FAILED, or CANCELLED)

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
   job result if job is inactive (COMPLETED, FAILED, CANCELLED), empty string otherwise

**result_abbrev**
   result but in a max 2 character abbreviation

**success**
   True of False if job completed successfully, empty string otherwise

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

**t_priority**
   time job entered priority state

**t_sched**
   time job entered sched state

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
have completed, failed, or were cancelled.  For example, the following
will list the jobs that have failed or were cancelled:

::

    $ flux jobs --filter=failed,cancelled

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

Github: http://github.com/flux-framework
