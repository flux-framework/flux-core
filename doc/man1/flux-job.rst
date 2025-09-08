.. flux-help-description: get job status, info, etc (see: flux help job)
.. flux-help-section: jobs

===========
flux-job(1)
===========


SYNOPSIS
========

| **flux** **job** **attach** [*--label-io*] [*-E*] [*--wait-event=EVENT*] *id*
| **flux** **job** **status** [*-v*] [*--json*] [-e CODE] *id [*id...*]
| **flux** **job** **last** [*N* | *SLICE*]
| **flux** **job** **urgency** [*-v*] *id* *N*
| **flux** **job** **wait** [*-v*] [*--all*] [*id*]
| **flux** **job** **kill** [*--signal=SIG*] *ids...*
| **flux** **job** **killall** [*-f*] [*--user=USER*] [*--signal=SIG*]
| **flux** **job** **raise** [*--severity=N*] [*--type=TYPE*] *ids...* [*--*] [*message...*]
| **flux** **job** **raiseall** [*--severity=N*] [*--user=USER*] [*--states=STATES*] *type* [ [*--*] [*message...*]
| **flux** **job** **taskmap** [*OPTIONS*] *id* | *taskmap*
| **flux** **job** **timeleft** [*-H*] [*id*]
| **flux** **job** **purge** [*-f*] [*--age-limit=FSD*] [*--num-limit=N*] [*ids...*]
| **flux** **job** **info** [*--original*] [*--base*] *id* *key*
| **flux** **job** **hostpids** [*OPTIONS*] *id*


DESCRIPTION
===========

:program:`flux job` performs various job related housekeeping functions.


OPTIONS
=======

.. program:: flux job

.. option:: -h, --help

   Display a list of :program:`flux job` sub-commands.


COMMANDS
========

Several subcommands are available to perform various operations on jobs.

attach
------

.. program:: flux job attach

A job can be interactively attached to via :program:`flux job attach`.  This is
typically used to watch stdout/stderr while a job is running or after it has
completed.  It can also be used to feed stdin to a job.

By default SIGTERM, SIGHUP, SIGALRM, SIGUSR1 and SIGUSR2 are forwarded to the
job when received by :program:`flux job attach`. To disable this behavior,
use the :option:`--read-only` option.

When :program:`flux job attach` receives two SIGINT (Ctrl-C) signals within 2s
it will cancel the job. A Ctrl-C followed by Ctrl-Z detaches from the job.
This behavior is also disabled with :option:`--read-only`.

When :program:`flux job attach` is run interactively -- that is all of
``stdout``, ``stderr`` and ``stdin`` are attached to a tty -- the command may
display a status line while the job is pending, e.g

::

    flux-job: ƒJqUHUCzX9 waiting for resources                 00:00:08

This status line may be suppressed by setting
:envvar:`FLUX_ATTACH_NONINTERACTIVE` in the environment.

.. option:: -l, --label-io

   Label output by rank

.. option:: -u, --unbuffered

   Do not buffer stdin. Note that when ``flux job attach`` is used in a
   terminal, the terminal itself may line buffer stdin.

.. option:: -i, --stdin-ranks=RANKS

   Send stdin to only those ranks in the **RANKS** idset. The standard input
   for tasks not in **RANKS** will be closed. The default is to broadcast
   stdin to all ranks.

.. option:: --tail[=NUM]

   Output only the last **NUM** lines of job output (default 10).

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

status
------

.. program:: flux job status

Wait for job(s) to complete and exit with the largest exit code.

.. option:: -e, --exception-exit-code=N

   Set the exit code for any jobs that terminate with an exception
   (e.g. canceled jobs) to ``N``.

.. option:: -j, --json

   Dump job result information from job eventlog.

.. option:: -v, --verbose

   Increase verbosity of output.

last
-----

.. program:: flux job last

Print the most recently submitted jobid for the current user.

If the optional argument is specified as a number *N*, print the *N* most
recently submitted jobids in reverse submission order, one per line.  If it
is enclosed in brackets, the argument is interpreted as a `python-style slice
<https://python-reference.readthedocs.io/en/latest/docs/brackets/slicing.html>`_
in :option:`[start:stop[:step]]` form which slices the job history array,
where index 0 is the most recently submitted job.

Examples:

:command:`flux job last 4`
  List the last four jobids in reverse submission order

:command:`flux job last [0:4]`
  Same as above

:command:`flux job last [-1:]`
  List the least recently submitted jobid

:command:`flux job last [:]`
  List all jobids in reverse submission order

:command:`flux job last [::-1]`
  List all jobids in submission order

urgency
-------

.. program:: flux job wait

:program:`flux job urgency` changes a job's urgency value.  The urgency
may also be specified at job submission time.  The argument *N* has a range
of 0 to 16 for guest users, or 0 to 31 for instance owners.  In lieu of a
numerical value, the following special names are also accepted:

hold (0)
  Hold the job until the urgency is raised with :option:`flux job urgency`.

default (16)
  The default urgency for all users.

expedite (31)
  Assign the highest possible priority to the job (restricted to instance
  owner).

Urgency is one factor used to calculate job priority, which affects the
order in which the scheduler considers jobs.  For more information, refer
to :man1:`flux-submit` description of the :option:`flux submit --urgency`
option.

wait
----

.. program:: flux job wait

:program:`flux job wait` behaves like the UNIX :linux:man2:`wait` system call,
for jobs submitted with the ``waitable`` flag.  Compared to other methods
of synchronizing on job completion and obtaining results, it is very
lightweight.

The result of a waitable job may only be consumed once.  This is a design
feature that makes it possible to call :program:`flux job wait` in a loop
until all results are consumed.

.. note::
  Only the instance owner is permitted to submit jobs with the ``waitable``
  flag.

When run with a jobid argument, :program:`flux job wait` blocks until the
specified job completes.  If the job was successful, it silently exits with a
code of zero.  If the job has failed, an error is printed on stderr, and it
exits with a code of one.  If the jobid is invalid or the job is not waitable,
:program:`flux job wait` exits with a code of two.  This special exit code of
two is used to differentiate between a failed job and not being able to wait
on the job.

When run without arguments, :program:`flux job wait` blocks until the next
waitable job completes and behaves as above except that the jobid is printed
to stdout.  When there are no more waitable jobs, it exits with a code of two.
The exit code of two can be used to determine when no more jobs are waitable
when using :program:`flux job wait` in a loop.

:option:`flux job wait --all` loops through all the waitable jobs as they
complete, printing their jobids.  If all jobs are successful, it exits with a
code of zero.  If any jobs have failed, it exits with a code of one.

.. option:: -a, --all

   Wait for all waitable jobs and exit with error if any jobs are
   not successful.

.. option:: -v, --verbose

   Emit a line of output for all jobs, not just failing ones.

kill
----

.. program:: flux job kill

One or more running jobs may be signaled by jobid with :program:`flux job kill`.

.. option:: -s, --signal=SIG

   Send signal SIG (default: SIGTERM).

killall
-------

.. program:: flux job killall

Running jobs may be signaled in bulk with :program:`flux job killall`.

.. option:: -u, --user=USER

   Set target user.  The instance owner may specify *all* for all users.

.. option:: -f, --force

   Confirm the command.

.. option:: -s, --signal=SIG

   Send signal SIG (default: SIGTERM).

raise
-----

.. program:: flux job raise

An exception may raised on one or more jobids with :program:`flux job raise`.
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

raiseall
--------

Exceptions may be raised in bulk with :program:`flux job raiseall`, which
requires a type (positional argument) and accepts the following options:

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

taskmap
-------

.. program:: flux job taskmap

The mapping between job task ranks to node IDs is encoded in the RFC 34
Flux Task Map format and posted to the job's ``shell.start`` event in the
exec eventlog. The :program:`flux job taskmap` utility is provided to assist in
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

.. option:: --to=raw|pmi|multiline|hosts

   Convert the taskmap to *raw* or *pmi* formats (described in RFC 34),
   *multiline* which prints the node ID of each task, one per line,
   or *hosts* which prints a list of taskids for each host. The default
   behavior is to print the RFC 34 taskmap. This option can be useful
   to convert between mapping forms, since :program:`flux job taskmap`
   can take a raw, pmi, or RFC 34 task map on the command line.

Only one of the above options may be used per call.

timeleft
--------

.. program:: flux job timeleft

The :program:`flux job timeleft` utility reports the number of whole seconds
left in the current or specified job time limit. If the job has expired or is
complete, then this command reports ``0``. If the job does not have a time
limit, then a large number (``UINT_MAX``) is reported.

If :program:`flux job timeleft` is called outside the context of a Flux job, or
an invalid or pending job is targeted, then this command will exit with
an error and diagnostic message.

Options:

.. option:: -H, --human

  Generate human readable output. Report results in Flux Standard Duration.

purge
-----

.. program:: flux job purge

Inactive job data may be purged from the Flux instance with
:program:`flux job purge`.  Specific job ids may be specified for purging.
If no job ids are specified, the following options may be used for selection
criteria:

.. option:: --age-limit=FSD

   Purge inactive jobs older than the specified Flux Standard Duration.

.. option:: --num-limit=COUNT

   Purge the oldest inactive jobs until there are at most COUNT left.

.. option:: -f, --force

   Confirm the command.

Inactive jobs may also be purged automatically if the job manager is
configured as described in :man5:`flux-config-job-manager`.


info
----

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

hostpids
--------

.. program:: flux job hostpids

:program:`flux job hostpids` prints a comma-delimited list of
``hostname:PID`` pairs for all tasks in a running job. If the job is
pending, :program:`flux job hostpids` will block until all tasks in the
job are running.

Options:

.. option:: -d, --delimiter=STRING

  Set the output delimiter to STRING (default=``,``).

.. option:: -r, --ranks=IDSET

  Restrict output to the task ranks in IDSET. The default is to display
  all ranks.

.. option:: -t, --timeout=DURATION

  Timeout the command after DURATION, which is specified in FSD.
  (a floating point value with optional suffix ``s`` for seconds,
   ``m`` for minutes, ``h`` for hours, or ``d`` for days).


RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_14`

:doc:`rfc:spec_18`

:doc:`rfc:spec_20`

:doc:`rfc:spec_21`

:doc:`rfc:spec_24`

:doc:`rfc:spec_25`

:doc:`rfc:spec_34`
