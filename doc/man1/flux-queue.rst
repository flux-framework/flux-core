.. flux-help-description: list and manipulate flux queues
.. flux-help-section: instance

=============
flux-queue(1)
=============


SYNOPSIS
========

**flux** **queue** **disable** [*--queue=NAME*] *reason...*

**flux** **queue** **enable** [*--queue=NAME*]

**flux** **queue** **status** [*--queue=NAME*]

**flux** **queue** **stop** [*--queue=NAME*]

**flux** **queue** **start** [*--queue=NAME*]

**flux** **queue** **drain** [*--timeout=DURATION*]

**flux** **queue** **idle** [*--timeout=DURATION*]

**flux** **queue** **list** [-n] [-o FORMAT]

DESCRIPTION
===========

.. program:: flux queue

The :program:`flux queue` command controls Flux job queues.

Normally, Flux has a single anonymous queue, but when queues are
configured, all queues are named.  At this time, the *disable*,
*enable*, *stop*, *start*, and *status* subcommands can be applied to
a single, named queue.  The rest affect all queues.

:program:`flux queue` has the following subcommands:

disable
  Prevent jobs from being submitted to the queue, with a reason that is
  shown to submitting users.  If multiple queues are configured, either the
  :option:`--queue` or the :option:`--all` option is required.

enable
  Allow jobs to be submitted to the queue.  If multiple queues are configured,
  either the :option:`--queue` or the :option:`--all` option is required.

status
  Report the current queue status.  If multiple queues are configured,
  all queues are shown unless one is specified with :option:`--queue`.

stop
  Stop allocating resources to jobs.  Pending jobs remain enqueued, and
  running jobs continue to run, but no new jobs are allocated
  resources.  If multiple queues are configured, either the :option:`--queue`
  or the :option:`--all` option is required.

start
  Start allocating resources to jobs.  If multiple queues are
  configured, either the :option:`--queue` or the :option:`--all` option is
  required.

drain
  Block until all queues become empty.  It is sometimes useful to run after
  :program:`flux queue disable`, to wait until the system is quiescent and can
  be taken down for maintenance.

idle
  Block until all queues become `idle` (no jobs in RUN or CLEANUP state,
  and no outstanding alloc requests to the scheduler).  It may be useful to run
  after :program:`flux queue stop` to wait until the scheduler and execution
  system are quiescent before maintenance involving them.

list
  Show queue defaults and limits. The :option:`--no-header` option suppresses
  header from output, :option:`--format=FORMAT`, customizes output formatting
  (see below).

OPTIONS
=======

.. option:: -h, --help

   Summarize available options.

.. option:: -q, --queue=NAME

   Select a queue by name.

.. option:: -v, --verbose

   Be chatty.

.. option:: --quiet

   Be taciturn.

.. option:: -a, --all

   Use with *enable*, *disable*, *stop*, or *start* subcommands to
   signify intent to affect all queues, when queues are configured but
   :option:`--queue` is missing.

.. option:: --nocheckpoint

   Use with *stop*, to not checkpoint that a queue has been stopped.
   This is often used when tearing down a flux instance, so that the a
   queue's start state is not assumed to be stopped on a restart.

.. option:: --timeout=FSD

   Limit the time that ``drain`` or ``idle`` will block.

.. option:: -n, --no-header

   Do not output column headers in ``list`` output.

.. option:: -o, --format=FORMAT

   Specify output format in ``list`` using Python's string format syntax.
   See `OUTPUT FORMAT`_ below for field names.


OUTPUT FORMAT
=============

The :option:`--format` option can be used to specify an output format using
Python's string format syntax or a defined format by name. For a list of
built-in and configured formats use :option:`-o help`.  An alternate default
format can be set via the FLUX_QUEUE_LIST_FORMAT_DEFAULT environment variable.
A configuration snippet for an existing named format may be generated with
:option:`--format=get-config=NAME`.  See :man1:`flux-jobs` *OUTPUT FORMAT*
section for a detailed description of this syntax.

The following field names can be specified:

**queue**
   queue name

**queuem**
   queue name, but default queue is marked up with an asterisk

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

Flux: http://flux-framework.org

RFC 23: Flux Standard Duration: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_23.html


SEE ALSO
========

:man1:`flux-jobs`, :man1:`flux-submit`
