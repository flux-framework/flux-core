.. flux-help-description: Manipulate flux queues

=============
flux-queue(1)
=============


SYNOPSIS
========

**flux** **queue** **disable** *reason...*

**flux** **queue** **enable**

**flux** **queue** **stop** [*--verbose*] [*--quiet*]

**flux** **queue** **start** [*--verbose*] [*--quiet*]

**flux** **queue** **status** [*--verbose*]

**flux** **queue** **drain** [*--timeout=DURATION*]

**flux** **queue** **idle** [*--quiet*] [*--timeout=DURATION*]

DESCRIPTION
===========

The ``flux-queue`` command controls the Flux job queue.
It has the following subcommands:

disable
  Prevent jobs from being submitted to the queue, with `reason` that is
  shown to submitting users.

enable
  Allow jobs to be submitted to the queue.

stop
  Stop allocating resources to jobs.  Pending jobs remain in the queue,
  and running jobs continue to run, but no new jobs are allocated resources.

start
  Start allocating resources to jobs.

status
  Report the current queue status.

drain
  Block until the queue becomes empty.  It is sometimes useful to run after
  ``flux queue disable``, to wait until the system is quiescent and can be
  taken down for maintenance.

idle
  Block until the queue becomes `idle` (no jobs in RUN or CLEANUP state,
  and no outstanding alloc requests to the scheduler).  It may be useful to run
  after ``flux queue stop`` to wait until the scheduler and execution system
  are quiescent before maintenance involving them.

OPTIONS
=======

**-h, --help**
   Summarize available options.

**-v, --verbose**
   Be chatty.

**-q, --quiet**
   Be taciturn.

**--timeout** \ =\ *FSD*
   Limit the time that ``drain`` or ``idle`` will block.


RESOURCES
=========

Flux: http://flux-framework.org

RFC 23: Flux Standard Duration: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_23.html


SEE ALSO
========

:man1:`flux-jobs`, :man1:`flux-mini`
