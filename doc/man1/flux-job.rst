.. flux-help-description: get job status, info, etc (see: flux help job)
.. flux-help-section: jobs

===========
flux-job(1)
===========


SYNOPSIS
========

**flux** **job** **attach** [*OPTIONS*] *id*

**flux** **job** **cancel** [*OPTIONS*] *ids...* [*--*] [*message...*]

**flux** **job** **cancelall** [*OPTIONS*] [*message...*]

**flux** **job** **status** [*OPTIONS*] *id [*id...*]

**flux** **job** **wait** [*OPTIONS*] [*id*]

**flux** **job** **kill** [*--signal=SIG*] *id* [*id...*]

**flux** **job** **killall** [*OPTIONS*]

**flux** **job** **raise** [*OPTIONS*] *ids...* [*--*] [*message...*]

**flux** **job** **raiseall** [*OPTIONS*] *type* [*message...*]

**flux** **job** **taskmap** [*OPTIONS*] *id*|*taskmap*

**flux** **job** **timeleft** [*OPTIONS*] [*id*]

**flux** **job** **purge** [*OPTIONS*] [*id...*]

**flux** **job** **info** [*OPTIONS*] *id* *key*


DESCRIPTION
===========

flux-job(1) performs various job related housekeeping functions.

ATTACH
======

.. program:: flux job attach

A job can be interactively attached to via ``flux job attach``.  This is
typically used to watch stdout/stderr while a job is running or after it has
completed.  It can also be used to feed stdin to a job.

When ``flux job attach`` is run interactively -- that is all of ``stdout``,
``stderr`` and ``stdin`` are attached to a tty -- the command may display
a status line while the job is pending, e.g

::

    flux-job: ƒJqUHUCzX9 waiting for resources                 00:00:08

This status line may be suppressed by setting ``FLUX_ATTACH_NONINTERACTIVE``
in the environment.

.. option:: -l, --label-io

   Label output by rank

.. option:: -u, --unbuffered

   Do not buffer stdin. Note that when ``flux job attach`` is used in a
   terminal, the terminal itself may line buffer stdin.

.. option:: -i, --stdin-ranks=RANKS

   Send stdin to only those ranks in the **RANKS** idset. The standard input
   for tasks not in **RANKS** will be closed. The default is to broadcast
   stdin to all ranks.

.. option:: --read-only

   Operate in read-only mode. Disable reading of stdin and capturing of
   signals.

.. option:: -v, --verbose

   Increase verbosity.

.. option:: -w, --wait-event=EVENT

   Wait for event *EVENT* before detaching from eventlog. The default is
   ``finish``.

.. option:: -E, --show-events

   Show job events on stderr. This option also suppresses the status line
   if enabled.

.. option:: -X, --show-exec

   Show exec eventlog events on stderr.

.. option:: --show-status

   Force immediate display of the status line.

.. option:: --debug

   Enable parallel debugger attach.

CANCEL
======

.. program:: flux job cancel

One or more jobs by may be canceled with ``flux job cancel``.  An optional
message included with the cancel exception may be provided via the
:option:`--message=NOTE` option or after the list of jobids. The special
argument *"--"* forces the end of jobid processing and can be used to separate
the exception message from the jobids when necessary.

.. option:: -m, --message=NOTE

   Set the optional exception note. It is an error to specify the message
   via this option and on the command line after the jobid list.

.. program:: flux job cancelall

Jobs may be canceled in bulk with ``flux job cancelall``.  Target jobs are
selected with:

.. option:: -u, --user=USER

   Set target user.  The instance owner may specify *all* for all users.

.. option:: -S, --states=STATES

   Set target job states (default: ACTIVE).

.. option:: -f, --force

   Confirm the command

.. option:: -q, --quiet

   Suppress output if no jobs match

STATUS
======

.. program:: flux job status

Wait for job(s) to complete and exit with the largest exit code.

.. option:: -e, --exception-exit-code=N

   Set the exit code for any jobs that terminate with an exception
   (e.g. canceled jobs) to ``N``.

.. option:: -j, --json

   Dump job result information from job eventlog.

.. option:: -v, --verbose

   Increase verbosity of output.

WAIT
====

.. program:: flux job wait

``flux job wait`` behaves like the UNIX :linux:man2:`wait` system call,
for jobs submitted with the ``waitable`` flag.  Compared to other methods
of synchronizing on job completion and obtaining results, it is very
lightweight.

The result of a waitable job may only be consumed once.  This is a design
feature that makes it possible to call ``flux job wait`` in a loop until all
results are consumed.

.. note::
  Only the instance owner is permitted to submit jobs with the ``waitable``
  flag.

When run with a jobid argument, ``flux job wait`` blocks until the specified
job completes.  If the job was successful, it silently exits with a code of
zero.  If the job has failed, an error is printed on stderr, and it exits with
a code of one.  If the jobid is invalid or the job is not waitable, ``flux job wait``
exits with a code of two.  This special exit code of two is used to differentiate
between a failed job and not being able to wait on the job.

When run without arguments, ``flux job wait`` blocks until the next waitable
job completes and behaves as above except that the jobid is printed to stdout.
When there are no more waitable jobs, it exits with a code of two.  The exit code
of two can be used to determine when no more jobs are waitable when using
``flux job wait`` in a loop.

:option:`flux job wait --all` loops through all the waitable jobs as they
complete, printing their jobids.  If all jobs are successful, it exits with a
code of zero.  If any jobs have failed, it exits with a code of one.

.. option:: -a, --all

   Wait for all waitable jobs and exit with error if any jobs are
   not successful.

.. option:: -v, --verbose

   Emit a line of output for all jobs, not just failing ones.

SIGNAL
======

.. program:: flux job kill

One or more running jobs may be signaled by jobid with ``flux job kill``.

.. option:: -s, --signal=SIG

   Send signal SIG (default: SIGTERM).

.. program:: flux job killall

Running jobs may be signaled in bulk with ``flux job killall``.  In addition
to the option above, target jobs are selected with:

.. option:: -u, --user=USER

   Set target user.  The instance owner may specify *all* for all users.

.. option:: -f, --force

   Confirm the command.

EXCEPTION
=========

.. program:: flux job raise

An exception may raised on one or more jobids with ``flux job raise``.
An optional message included with the job exception may be provided via
the :option:`--message=NOTE` option or after the list of jobids. The special
argument *"--"* forces the end of jobid processing and can be used to
separate the exception message from the jobids when necessary.

.. option:: -m, --message=NOTE

   Set the optional exception note. It is an error to specify the message
   via this option and on the command line after the jobid list.

.. option:: -s, --severity=N

   Set exception severity.  The severity may range from 0=fatal to
   7=least severe (default: 0).

.. option:: -t, --type=TYPE

   Set exception type (default: cancel).

Exceptions may be raised in bulk with ``flux job raiseall``, which requires a
type (positional argument) and accepts the following options:

.. program:: flux job raiseall

.. option:: -s, --severity=N

   Set exception severity.  The severity may range from 0=fatal to
   7=least severe (default: 7).

.. option:: -u, --user=USER

   Set target user.  The instance owner may specify *all* for all users.

.. option:: -S, --states=STATES

   Set target job states (default: ACTIVE)

.. option:: -f, --force

   Confirm the command.

TASKMAP
=======

.. program:: flux job taskmap

The mapping between job task ranks to node IDs is encoded in the RFC 34
Flux Task Map format and posted to the job's ``shell.start`` event in the
exec eventlog. The ``flux job taskmap`` utility is provided to assist in
working with these task maps.

When executed with a jobid argument and no options, the taskmap for the job
is printed after the ``shell.start`` event has been posted.

With one of the following arguments, the job taskmap may be used to convert
a nodeid to a list of tasks, or to query on which node or host a given
taskid ran. The command may also be used to convert between different
support task mapping formats:

.. option:: --taskids=NODEID

   Print an idset of tasks which ran on node  *NODEID*

.. option:: --ntasks=NODEID

   Print the number of tasks  which ran on node *NODEID*

.. option:: --nodeid=TASKID

   Print the node ID that ran task *TASKID*

.. option:: --hostname=TASKID

   Print the hostname of the node that rank task *TASKID*

.. option:: --to=raw|pmi|multiline

   Convert the taskmap to *raw* or *pmi* formats (described in RFC 34), or
   *multiline* which prints the node ID of each task, one per line.

One one of the above options may be used per call.

TIMELEFT
========

.. program:: flux job timeleft

The ``flux job timeleft`` utility reports the number of whole seconds left
in the current or specified job time limit. If the job has expired or is
complete, then this command reports ``0``. If the job does not have a time
limit, then a large number (``UINT_MAX``) is reported.

If ``flux job timeleft`` is called outside the context of a Flux job, or
an invalid or pending job is targeted, then this command will exit with
an error and diagnostic message.

Options:

.. option:: -H, --human

  Generate human readable output. Report results in Flux Standard Duration.

PURGE
=====

.. program:: flux job purge

Inactive job data may be purged from the Flux instance with ``flux job purge``.
Specific job ids may be specified for purging.  If no job ids are
specified, the following options may be used for selection criteria:

.. option:: --age-limit=FSD

   Purge inactive jobs older than the specified Flux Standard Duration.

.. option:: --num-limit=COUNT

   Purge the oldest inactive jobs until there are at most COUNT left.

.. option:: -f, --force

   Confirm the command.

Inactive jobs may also be purged automatically if the job manager is
configured as described in :man5:`flux-config-job-manager`.


flux job info
-------------

.. program:: flux job info

:program:`flux job info` retrieves the selected low level job object
and displays it on standard output.  Object formats are described in the
RFCs listed in `RESOURCES`_.


Options:

.. option:: -o, --original

  For :option:`jobspec`, return the original submitted jobspec, prior
  to any modifications made at ingest, such as setting defaults.

.. option:: -b, --base

  For :option:`jobspec` or :option:`R`, return the base version, prior
  to any updates posted to the job eventlog.

The following keys are valid:

eventlog
   The primary job eventlog, consisting of timestamped events that drive the
   job through various states.  For example, a job that is pending resource
   allocation in SCHED state transitions to RUN state on the *alloc* event.

guest.exec.eventlog
   The execution eventlog, consisting of timestamped events posted by the
   execution system while the job is running.

guest.input, guest.output
   The job input and output eventlogs, consisting of timestamped chunks of
   input/output data.

jobspec
   The job specification.  Three versions are available:

   - default: the *current* jobspec, which may reflect updates,
     for example if the job duration was extended

   - with :option:`--original`: the original jobspec submitted by the user

   - with :option:`--base`: the jobspec as initially ingested to the KVS, after
     the frobnicator filled in any default values, but before updates

R
   The resource set allocated to the job.  Two versions are available:

   - default: the *current* R, which may reflect updates, for example if the job
     expiration time was extended (default)

   - with :option:`--base`: the initial R allocated by the scheduler


RESOURCES
=========

Flux: http://flux-framework.org

:doc:`rfc:spec_14`
  https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_14.html

:doc:`rfc:spec_18`
  https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_18.html

:doc:`rfc:spec_20`
  https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_20.html

:doc:`rfc:spec_21`
  https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_21.html

:doc:`rfc:spec_24`
  https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_24.html

:doc:`rfc:spec_25`
  https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_25.html

:doc:`rfc:spec_34`
  https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_34.html
