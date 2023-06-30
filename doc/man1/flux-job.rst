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

DESCRIPTION
===========

flux-job(1) performs various job related housekeeping functions.

ATTACH
======

A job can be interactively attached to via ``flux job attach``.  This is
typically used to watch stdout/stderr while a job is running or after it has
completed.  It can also be used to feed stdin to a job.

**-l, --label-io**
   Label output by rank

**-u, --unbuffered**
   Do not buffer stdin. Note that when ``flux job attach`` is used in a
   terminal, the terminal itself may line buffer stdin.

**-i, --stdin-ranks=RANKS**
   Send stdin to only those ranks in the **RANKS** idset. The standard input
   for tasks not in **RANKS** will be closed. The default is to broadcast
   stdin to all ranks.

CANCEL
======

One or more jobs by may be canceled with ``flux job cancel``.  An optional
message included with the cancel exception may be provided via the *-m,
--message=NOTE* option or after the list of jobids. The special argument
*"--"* forces the end of jobid processing and can be used to separate the
exception message from the jobids when necessary.

**-m, --message=NOTE**
   Set the optional exception note. It is an error to specify the message
   via this option and on the command line after the jobid list.

Jobs may be canceled in bulk with ``flux job cancelall``.  Target jobs are
selected with:

**-u, --user=USER**
   Set target user.  The instance owner may specify *all* for all users.

**-S, --states=STATES**
   Set target job states (default: ACTIVE).

**-f, --force**
   Confirm the command

**-q, --quiet**
   Suppress output if no jobs match

STATUS
======

Wait for job(s) to complete and exit with the largest exit code.

**-e, --exception-exit-code=N**
   Set the exit code for any jobs that terminate with an exception
   (e.g. canceled jobs) to ``N``.

**-j, --json**
   Dump job result information from job eventlog.

**-v, --verbose**
   Increase verbosity of output.

WAIT
====

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

``flux job wait --all`` loops through all the waitable jobs as they complete,
printing their jobids.  If all jobs are successful, it exits with a code of zero.
If any jobs have failed, it exits with a code of one.

**-a, --all**
   Wait for all waitable jobs and exit with error if any jobs are
   not successful.

**-v, --verbose**
   Emit a line of output for all jobs, not just failing ones.

SIGNAL
======

One or more running jobs may be signaled by jobid with ``flux job kill``.

**-s, --signal=SIG**
   Send signal SIG (default: SIGTERM).

Running jobs may be signaled in bulk with ``flux job killall``.  In addition
to the option above, target jobs are selected with:

**-u, --user=USER**
   Set target user.  The instance owner may specify *all* for all users.

**-f, --force**
   Confirm the command.

EXCEPTION
=========

An exception may raised on one or more jobids with ``flux job raise``.
An optional message included with the job exception may be provided via
the *-m, --message=NOTE* option or after the list of jobids. The special
argument *"--"* forces the end of jobid processing and can be used to
separate the exception message from the jobids when necessary.

**-m, --message=NOTE**
   Set the optional exception note. It is an error to specify the message
   via this option and on the command line after the jobid list.
**-s, --severity=N**
   Set exception severity.  The severity may range from 0=fatal to
   7=least severe (default: 0).

**-t, --type=TYPE**
   Set exception type (default: cancel).

Exceptions may be raised in bulk with ``flux job raiseall``, which requires a
type (positional argument) and accepts the following options:

**-s, --severity=N**
   Set exception severity.  The severity may range from 0=fatal to
   7=least severe (default: 7).

**-u, --user=USER**
   Set target user.  The instance owner may specify *all* for all users.

**-S, --states=STATES**
   Set target job states (default: ACTIVE)

**-f, --force**
   Confirm the command.

TASKMAP
=======

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

**--taskids=NODEID**
   Print an idset of tasks which ran on node  *NODEID*

**--ntasks=NODEID**
   Print the number of tasks  which ran on node *NODEID*

**--nodeid=TASKID**
   Print the node ID that ran task *TASKID*

**--hostname=TASKID**
   Print the hostname of the node that rank task *TASKID*

**--to=raw|pmi|multiline**
   Convert the taskmap to *raw* or *pmi* formats (described in RFC 34), or
   *multiline* which prints the node ID of each task, one per line.

One one of the above options may be used per call.

TIMELEFT
========

The ``flux job timeleft`` utility reports the number of whole seconds left
in the current or specified job time limit. If the job has expired or is
complete, then this command reports ``0``. If the job does not have a time
limit, then a large number (``UINT_MAX``) is reported.

If ``flux job timeleft`` is called outside the context of a Flux job, or
an invalid or pending job is targeted, then this command will exit with
an error and diagnostic message.

Options:

**-H, --human**
  Generate human readable output. Report results in Flux Standard Duration.

PURGE
=====

Inactive job data may be purged from the Flux instance with ``flux job purge``.
Specific job ids may be specified for purging.  If no job ids are
specified, the following options may be used for selection criteria:

**--age-limit=FSD**
   Purge inactive jobs older than the specified Flux Standard Duration.

**--num-limit=COUNT**
   Purge the oldest inactive jobs until there are at most COUNT left.

**-f, --force**
   Confirm the command.

Inactive jobs may also be purged automatically if the job manager is
configured as described in :man5:`flux-config-job-manager`.


RESOURCES
=========

Flux: http://flux-framework.org

RFC 34: Flux Task Map: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_34.html

