.. flux-help-description: display running Flux jobs
.. flux-help-section: jobs

===========
flux-top(1)
===========


SYNOPSIS
========

**flux** **top** [*OPTIONS*] [*TARGET*]


DESCRIPTION
===========

.. program:: flux top

The :program:`flux top` command provides a dynamic view of Flux instance status
and running jobs.  *TARGET*, if specified, selects a Flux instance other
than the default, and may be either a native Flux URI or a high level URI,
as described in :man1:`flux-uri`.

The :program:`flux top` display window is divided into two parts:  the summary
pane, and the job listing pane, which are described in detail below.


OPTIONS
=======

.. option:: -h, --help

   Summarize available options.

.. option:: --color[=WHEN]

  .. include:: common/color.rst

.. option:: -q, --queue=NAME

   Limit status and jobs to specific queue.


KEYS
====

:program:`flux top` responds to the following key presses:

j, down-arrow
   Move cursor down in the job listing.

k, up-arrow
   Move cursor up in the job listing.

h/l, left-arrow/right-arrow
   Rotate through all queues on the system.

d
   Toggle display of inactive job details (failed vs successful jobs).

enter
   Open the job at the current cursor position.  Only Flux instances (colored
   blue in the job listing) owned by the user running :program:`flux top` may
   be opened.  The display changes to show a new Flux instance, with its jobid
   added to the path in the summary pane.  Nothing happens if the selected
   job cannot be opened.

q
   Quit the current Flux instance, popping back to the previous Flux instance,
   if any.  If the original Flux instance is being displayed, quit the program.

control-l
   Force a redraw of the :program:`flux top` window.


SUMMARY PANE
============

The summary pane shows the following information:

- The path of nested job ID's, if navigating the job hierarchy with the *up*,
  *down*, *enter*, and *q* keys.

- The amount of time until the job's expiration time, in Flux Standard Duration
  format.  If the expiration time is unknown, the infinity symbol is
  displayed (see `CAVEATS`_ below).

- The nodes bargraph, which shows the fraction of used and down/excluded nodes
  vs total nodes.  The graph of used nodes is colored yellow and extends from
  left to right.  The graph of down/excluded nodes is red and extends from
  right to left.

- The cores bargraph, with the same layout as the nodes bargraph.

- The gpus bargraph, with the same layout as the nodes bargraph.

- The number of pending, running, and inactive jobs. When executed as the
  instance owner, inactive jobs are split into completed (successful) and
  failed (unsuccessful and canceled) jobs. This display can be toggled with
  the ``d`` key.

- A heart icon that appears each time the instance heartbeat event is
  published.

- The instance size.  This is the total number of brokers, which is usually
  also the number of nodes.

- The Flux instance depth, if greater than zero.  If the Flux instance was
  not launched as a job within another Flux instance, the depth is zero.

- The elapsed time the Flux instance has been running, in RFC 23 Flux Standard
  Duration format.

- The flux-core software version.


JOB LIST PANE
=============

The job listing pane shows running jobs, that is, jobs in *RUN* (R) or
*CLEANUP* (C) state.  Jobs that are Flux instances are shown in blue and
may be selected for display, as described in the KEYS section.

The columns of output are currently fixed, and use the same naming convention
as :man1:`flux-jobs`.

The newest jobs are shown at the top of the display.

:program:`flux top` subscribes to job state update events, and tries to update
its display within 2s of receiving new job information.


CAVEATS
=======

:program:`flux top` employs a few UTF-8 characters to maximize cuteness.  If
your heart emoji looks like a cartoon expletive, consult your system
administrator.

The infinity symbol in the expiration field does not really mean the Flux
instance will run forever.  The field width of the timestamp portion of the
Flux Locally Unique IDs (RFC 19) used for job IDs places an outer bound on
a Flux instance's lifetime of about 35 years.


RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_19`

:doc:`rfc:spec_23`


SEE ALSO
========

:man1:`flux-resource`, :man1:`flux-uptime`, :man1:`flux-jobs`, :man1:`flux-uri`
