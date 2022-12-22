.. flux-help-include: true

===========
flux-job(1)
===========


SYNOPSIS
========

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

**flux** **job** **purge** [*OPTIONS*]

DESCRIPTION
===========

flux-job(1) performs various job related housekeeping functions.

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

A waitable job may be waited on with ``flux job wait``.  A specific job
can be waited on by specifying a jobid.  If no jobid is specified, the
command will wait for any waitable user job to complete, outputting that
jobid before exiting.  This command will exit with error if the job is not
successful.

Compared to ``flux job status``, there are several advantages /
disadvantages of using ``flux job wait``.  For a large number of jobs,
``flux job wait`` is far more efficient, especially when used with the
``--all`` option below.  In addition, job ids do not have to be specified
to ``flux job wait``.

The two major limitations are that jobs must be submitted with the
waitable flag, which can only be done in user instances.  In addition,
``flux job wait`` can only be called once per job.

**-a, --all**
   Wait for all waitable jobs.  Will exit with error if any jobs are
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
The following options may be used to add selection criteria:

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

