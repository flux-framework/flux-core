.. flux-help-description: Manipulate flux queues

=============
flux-queue(1)
=============


SYNOPSIS
========

**flux** **queue** **disable** [*--queue=NAME*] *reason...*

**flux** **queue** **enable** [*--queue=NAME*]

**flux** **queue** **status** [*--queue=NAME*]

**flux** **queue** **stop**

**flux** **queue** **start**

**flux** **queue** **drain** [*--timeout=DURATION*]

**flux** **queue** **idle** [*--timeout=DURATION*]

DESCRIPTION
===========

The ``flux-queue`` command controls Flux job queues.

Normally, Flux has a single anonymous queue, but when queues are configured,
all queues are named.  At this time, only the *disable*, *enable*, and
*status* subcommands can be applied to a single, named queue.  The rest affect
all queues.

``flux-queue`` has the following subcommands:

disable
  Prevent jobs from being submitted to the queue, with a reason that is
  shown to submitting users.  If multiple queues are configured, either the
  *--queue* or the *--all* option is required.

enable
  Allow jobs to be submitted to the queue.  If multiple queues are configured,
  either the *--queue* or the *--all* option is required.

status
  Report the current queue status.  If multiple queues are configured,
  all queues are shown unless one is specified with *--queue*.

stop
  Stop allocating resources to jobs.  Pending jobs remain enqueued,
  and running jobs continue to run, but no new jobs are allocated resources.

start
  Start allocating resources to jobs.

drain
  Block until all queues become empty.  It is sometimes useful to run after
  ``flux queue disable``, to wait until the system is quiescent and can be
  taken down for maintenance.

idle
  Block until all queues become `idle` (no jobs in RUN or CLEANUP state,
  and no outstanding alloc requests to the scheduler).  It may be useful to run
  after ``flux queue stop`` to wait until the scheduler and execution system
  are quiescent before maintenance involving them.

OPTIONS
=======

**-h, --help**
   Summarize available options.

**-q, --queue**\ =\ *NAME*
   Select a queue by name.

**-v, --verbose**
   Be chatty.

**--quiet**
   Be taciturn.

**-a, --all**
   Use with *enable* or *disable* subcommands to signify intent to affect
   all queues, when queues are configured but *--queue* is missing.

**--timeout** \ =\ *FSD*
   Limit the time that ``drain`` or ``idle`` will block.


RESOURCES
=========

Flux: http://flux-framework.org

RFC 23: Flux Standard Duration: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_23.html


SEE ALSO
========

:man1:`flux-jobs`, :man1:`flux-mini`
