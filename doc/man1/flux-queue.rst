.. flux-help-description: list and manipulate flux queues
.. flux-help-section: instance

=============
flux-queue(1)
=============


SYNOPSIS
========

| **flux** **queue** **list** [-n] [-o FORMAT]
| **flux** **queue** **status** [*-q* *NAME* | *-a*] [*-v*]

| **flux** **queue** **disable** [*-q* *NAME* | *-a*] [*-v*] [*--quiet*] [*--nocheckpoint*] *reason*
| **flux** **queue** **enable** [*-q* *NAME* | *-a*] [*-v*] [*--quiet*]

| **flux** **queue** **stop** [*-q* *NAME* | *-a*] [*-v*] [*--quiet*] [*--nocheckpoint*] *reason*
| **flux** **queue** **start** [*-q* *NAME* | *-a*] [*-v*] [*--quiet*]

| **flux** **queue** **drain** [*--timeout=DURATION*]
| **flux** **queue** **idle** [*--timeout=DURATION*]


DESCRIPTION
===========

.. program:: flux queue

The :program:`flux queue` command operates on Flux job queue(s).

By default, Flux has one anonymous queue.  Multiple named queues may be
configured - see :man5:`flux-config-queues`.

COMMANDS
========

:program:`flux queue` has the following subcommands:

list
----

.. program:: flux queue list

List queue status, defaults, and limits.

.. option:: -q, --queue=QUEUE,...

   Limit output to specified queues

.. option:: -n, --no-header

   Do not output column headers in ``list`` output.

.. option:: -o, --format=FORMAT

   Specify output format in ``list`` using Python's string format syntax.
   See `OUTPUT FORMAT`_ below for field names.

status
------

.. program:: flux queue status

Report the current queue status.

.. option:: -q, --queue=NAME

   Select a queue by name.

.. option:: -v, --verbose

   Display more detail about internal job manager state.

disable
-------

.. program:: flux queue disable

Prevent jobs from being submitted to the queue, with a reason that is
shown to submitting users.

.. option:: -q, --queue=NAME

   Select a queue by name.

.. option:: -a, --all

   Select all queues.

.. option:: -v, --verbose

   Display more detail about internal job manager state.

.. option:: --quiet

   Display only errors.

.. option:: --nocheckpoint

   Do not preserve the new queue stop state across a Flux instance restart.

enable
------

.. program:: flux queue enable

Allow jobs to be submitted to the queue.

.. option:: -q, --queue=NAME

   Select a queue by name.

.. option:: -a, --all

   Select all queues.

.. option:: -v, --verbose

   Display more detail about internal job manager state.

.. option:: --quiet

   Display only errors.

stop
----

.. program:: flux queue stop

Stop allocating resources to jobs.  Pending jobs remain enqueued, and
running jobs continue to run, but no new jobs are allocated
resources.

.. option:: -q, --queue=NAME

   Select a queue by name.

.. option:: -a, --all

   Select all queues.

.. option:: -v, --verbose

   Display more detail about internal job manager state.

.. option:: --quiet

   Display only errors.

.. option:: --nocheckpoint

   Do not preserve the new queue stop state across a Flux instance restart.

start
-----

.. program:: flux queue start

Start allocating resources to jobs.

.. option:: -q, --queue=NAME

   Select a queue by name.

.. option:: -a, --all

   Select all queues.

.. option:: -v, --verbose

   Display more detail about internal job manager state.

.. option:: --quiet

   Display only errors.

drain
-----

.. program:: flux queue drain

Block until all queues become empty.  It is sometimes useful to run after
:program:`flux queue disable`, to wait until the system is quiescent and can
be taken down for maintenance.

.. option:: --timeout=FSD

   Limit the time that the command` will block.

idle
----

.. program:: flux queue idle

Block until all queues become `idle` (no jobs in RUN or CLEANUP state,
and no outstanding alloc requests to the scheduler).  It may be useful to run
after :program:`flux queue stop` to wait until the scheduler and execution
system are quiescent before maintenance involving them.

.. option:: --timeout=FSD

   Limit the time that the command` will block.


OUTPUT FORMAT
=============

The :option:`flux queue list --format` option can be used to specify an
output format using Python's string format syntax or a defined format by
name. For a list of built-in and configured formats use :option:`-o help`.
An alternate default format can be set via the
:envvar:`FLUX_QUEUE_LIST_FORMAT_DEFAULT` environment variable.
A configuration snippet for an existing named format may be
generated with :option:`--format=get-config=NAME`.  See :man1:`flux-jobs`
*OUTPUT FORMAT* section for a detailed description of this syntax.

The following field names can be specified:

**queue**
   queue name

**queuem**
   queue name, but default queue is marked up with an asterisk

**submission**
   Description of queue submission status: ``enabled`` or ``disabled``

**scheduling**
   Description of queue scheduling status: ``started`` or ``stopped``

**enabled**
   Single character submission status: ``✔`` if enabled, ``✗`` if disabled.

**started**
   Single character scheduling status: ``✔`` if started, ``✗`` if stopped.

**enabled.ascii**
   Single character submission status: ``y`` if enabled, ``n`` if disabled.

**started.ascii**
   Single character scheduling status: ``y`` if started, ``n`` if stopped.

**defaults.timelimit**
   default timelimit for jobs submitted to the queue

**limits.timelimit**
   max timelimit for jobs submitted to the queue

**limits.range.nnodes**
   range of nodes that can be requested for this queue

**limits.range.ncores**
   range of cores that can be requested for this queue

**limits.range.ngpus**
   range of gpus that can be requested for this queue

**limits.min.nnodes**
   minimum number of nodes that must be requested for this queue

**limits.max.nnodes**
   maximum number of nodes that can be requested for this queue

**limits.min.ncores**
   minimum number of cores that must be requested for this queue

**limits.max.ncores**
   maximum number of cores that can be requested for this queue

**limits.min.ngpus**
   minimum number of gpus that must be requested for this queue

**limits.max.ngpus**
   maximum number of gpus that can be requested for this queue


RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

| :doc:`rfc:spec_23`
| :doc:`rfc:spec_33`


SEE ALSO
========

:man1:`flux-jobs`, :man1:`flux-submit`
