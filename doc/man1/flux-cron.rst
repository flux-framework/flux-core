============
flux-cron(1)
============


SYNOPSIS
========

| **flux** **cron** **tab** [*-E*] [*-d* *DIR*] [*-o* *OPT...*] [*file*]
| **flux** **cron** **at** [*-E*] [*-d* *DIR*] [*-o* *OPT...*] *time* *command*
| **flux** **cron** **event** [*-E*] [*-d* *DIR*] [*-o* *OPT...*] *topic* *command*
| **flux** **cron** **interval** [*-E*] [*-d* *DIR*] [*-o* *OPT...*] *interval* *command*

| **flux** **cron** **list**
| **flux** **cron** **stop** *ids...*
| **flux** **cron** **start** *ids...*
| **flux** **cron** **delete** [*--kill*] *ids...*
| **flux** **cron** **dump** [*--key=KEY*] *ids...*
| **flux** **cron** **sync** [*--disable*] [*--epsilon=TIME*] *topic*


DESCRIPTION
===========

The Flux cron service offers an interface for executing commands on
triggers such as a time interval or Flux events. The service is
implemented as a Flux broker module which, when loaded, manages
a set of cron entries and uses the built-in *broker.exec* service to run
a command associated with the entry each time the defined trigger is
reached. As with :man1:`flux-exec`, these tasks run as direct children
of the :man1:`flux-broker` and run outside of the control of any loaded
job scheduling service.

The :program:`flux cron` utility offers an interface to create, stop, start,
query, and destroy these entries in the Flux cron service.

For a detailed description of the cron service operation and how
it executes tasks, see the `OPERATION`_ and `TASK EXECUTION`_ sections
below.


COMMANDS
========

The following sub-commands are available:

tab
---

.. program:: flux cron tab

Process one or more lines containing crontab expressions from *file*
(stdin by default).  Each valid crontab line will result in a new cron
entry registered with the Flux cron service.

Crontab lines have five standard fields, similar to :linux:man5:`crontab`:

.. list-table::

   * - minutes
     - 0-59

   * - hours
     - 0-23

   * - day of month
     - 0-23

   * - month
     - 0-11

   * - day of week
     - 0-6

Everything after the day of week is considered a command to be run.

.. option:: -o, options=LIST

  Set comma separated `EXTRA OPTIONS`_ for all cron entries.

.. option:: -E, --preserve-env

  Export the current environment to be used for the command being executed
  as part of the cron job.  Normally, the broker environment is used.

.. option:: -d, --working-dir=DIR

  Set the working directory for commands being executed as part
  of the cron job. Normally, the working directory of the broker is used.

at
--

.. program:: flux cron at

Run *command* at specific date and time described by *time*.  Any time
string that can be parsed by :linux:man1:`date` is acceptable.

.. option:: -o, options=LIST

  Set comma separated `EXTRA OPTIONS`_ for all cron entries.

.. option:: -E, --preserve-env

  Export the current environment to be used for the command being executed
  as part of the cron job.  Normally, the broker environment is used.

.. option:: -d, --working-dir=DIR

  Set the working directory for the command being executed as part
  of the cron job. Normally, the working directory of the broker is used.

event
-----

.. program:: flux cron event

Create a cron entry to execute *command* after every event matching *topic*.

.. option:: -N, --name=STRING

  Set a name for this cron entry to *STRING*.

.. option:: -n, --nth=N

  If :option:`--nth` is given then *command* will be run after each *N* events.

.. option:: -c, --count=N

  With :option:`--count`, the entry is run *N* times then stopped.

.. option:: -a, --after=N

  Run the first task only after *N* matching events. Then run every event
  or *N* events with :option:`--nth`.

.. option:: -i, --min-interval=T

  Set the minimum interval at which two cron jobs for this event will be run.
  For example, with :option:`--min-interval` of 1s, the cron job will be
  at most run every 1s, even if events are generated more quickly.

.. option:: -o, --options=LIST

  Set comma separated `EXTRA OPTIONS`_ for this cron entry.

.. option:: -E, --preserve-env

  Export the current environment to be used for the command being executed
  as part of the cron job.  Normally, the broker environment is used.

.. option:: -d, --working-dir=DIR

  Set the working directory for the command being executed as part
  of the cron job. Normally, the working directory of the broker is used.

interval
--------

.. program:: flux cron interval

Create a cron entry to execute *command* every *interval*, where *interval*
is an arbitrary floating point duration with optional suffix *s* for
seconds, *m* for minutes, *h* for hours and *d* for days.  If no suffix is
specified, seconds is assumed.

.. option:: -n, --name=STRING

  Set a name for this cron entry to *STRING*.

.. option:: -a, --after=TIME

  The first task will run after a delay of *TIME* instead of *interval*, where
  *TIME* is an arbitrary floating point duration specified in the same format
  as *interval*.  After the first task the entry will continue to execute
  every *interval*.

.. option:: -c, --count=N

  The entry will be run a total of *N* times, then stopped.

.. option:: -o, --options=LIST

  Set comma separated `EXTRA OPTIONS`_ for this cron entry.

.. option:: -E, --preserve-env

  Export the current environment to be used for the command being executed
  as part of the cron job.  Normally, the broker environment is used.

.. option:: -d, --working-dir=DIR

  Set the working directory set for the command being executed as part
  of the cron job. Normally, the working directory of the broker is used.


list
----

.. program:: flux cron list

Display a list of current entries registered with the cron module and
their current state, last run time, etc.

stop
----

.. program:: flux cron stop

Stop cron entry *id*. The entry will remain in the cron entry list until
deleted.

start
-----

.. program:: flux cron start

Start a stopped cron entry *id*.

delete
------

.. program:: flux cron delete

Purge cron entry *id* from the cron entry list.

.. option:: -k, --kill

Kill any running task associated with entry *id*.

dump
----

.. program:: flux cron dump

Dump all information for cron entry *id*.

.. option:: -k, --key=KEY

Print only the value for key *KEY*.

For a list of keys run :option:`flux cron dump ID`.

sync
----

.. program flux cron sync

Query and modify the current **sync-event** behavior for the cron module.
If a sync-event is set, the cron module will defer all task execution
until an event matching the sync-event *topic* is received.

Without any *topic* supplied on command line, :program:`flux cron sync`
displays the current setting for sync.

If a task is deferred due to sync-event, the *stats.deferred* statistic
is incremented.

.. option:: -e, --epsilon=TIME

  Set amount of time after a *sync-event* that jobs are still allowed to
  be run.  With this option, the cron module will **not** delay task execution
  if the task is normally scheduled to run within *delay* of the matching
  event.

.. option:: -d, --disable

  Disable the cron *sync-event*.


EXTRA OPTIONS
=============

.. program:: flux cron tab

For :program:`flux cron` commands allowing :option:`--options`, the following
extra options are supported:

.. option:: -o timeout=N

   Set a timeout for tasks invoked for this cron entry to *N* seconds, where
   N can be a floating point number. Default is no timeout.

.. option:: -o rank=R

   Set the rank on which to execute the cron command to *R*. Default is rank 0.

.. option:: -o task-history-count=N

   Keep history for the last *N* tasks invoked by this cron entry. Default is 1.

.. option:: -o stop-on-failure=N

   Automatically stop a cron entry if the failure count exceeds *N*. If *N* is
   zero (the default) then the cron entry will not be stopped on failure.

OPERATION
=========

The Flux cron module manages the set of currently configured cron
jobs as a set of common entries, each with a unique ID supplied by
a global sequence number and set of common attributes, options, and
statistics. Basic attributes of a cron job include an optional *name*,
the *command* to execute on the entry's trigger, the current *state* of
the cron entry (stopped or not stopped), a *repeat* count indicating the
total number of times to execute the cron job before stopping, and the
*type* of entry.

All cron entries also support a less common list of options, which may
be set at creation time via a comma-separated list of *option=value*
parameters passed to :option:`-o, --option=OPTS`. These options are described
in the EXTRA OPTIONS section at the end of this document.

Currently, Flux cron supports only two types of entries. The *interval*
entry supports executing a command once every configured duration,
optionally starting after a different time period. More detailed
information about the interval type can be found in the documentation for
the :program:`flux cron interval` command above. The *event* type entry supports
running a command once every N events matching the configured event topic.
More information about this type can be found in the documentation for
:program:`flux cron event`.

The Flux cron module additionally keeps a common set of statistics for
each entry, regardless of type . These include the creation time, last
run time, and last time the cron entry was "started", as well a count of
total number of times the command was executed and a count of successful
and failed runs. Currently, the stats for a cron entry may be viewed via
the *flux cron dump* subcommand *stats.\** output.

When registered, cron entries are automatically *started*, meaning they
are eligible to run the configured command when the trigger condition
is met. Entries may be *stopped*, either by use of the :program:`flux cron stop`
command, or if a *stop-on-failure* value is set. Stopped entries are
restarted using :program:`flux cron start`, at which point counters used for
repeat and stop-on-failure are reset.

Stopped entries are kept in the flux cron until deleted with
:program:`flux cron delete`. Active cron entries may also be deleted, with
currently executing tasks optionally killed if the :option:`--kill` option is
provided.


TASK EXECUTION
==============

As related above, cron entry commands are executed via the *broker.exec*
service, which is a low level execution service offered outside of any
scheduler control, described in more detail in the *flux-exec(1)* man
page.

Standard output and error from tasks executed by the cron service are
logged and may be viewed with :man1:`flux-dmesg`. If a cron task exits
with non-zero status, or fails to launch under the *broker.exec* service,
a message is logged and the failure is added to the failure stats.
On task failure, the cron job is stopped if *stop-on-failure* is set, and
the current failure count exceeds the configured value. By default,
*stop-on-failure* is not set.

By default, the Flux cron module keeps information for the last task executed
for each cron entry. This information can be viewed either via the
:program:`flux cron list` or :program:`flux cron dump ID` subcommands. Data
such as start and end time, exit status, rank, and PID for the task is
available.  The number of tasks kept for each cron entry may be individually
tuned via the :option:`--task-history-count` option, described in the
EXTRA OPTIONS section.

Commands are normally executed immediately on the interval or event
trigger for which they are configured. However, if the :option:`--sync-event`
option is active on the cron module, tasks execution will be deferred
until the next synchronization event. See the documentation above
for :program:`flux cron sync` for more information.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man1:`flux-exec`, :man1:`flux-dmesg`
