====================
flux-housekeeping(1)
====================


SYNOPSIS
========

| **flux** **housekeeping** **list** [*-n*] [*-o FORMAT*]
| **flux** **housekeeping** **kill** [*--all*] [*-j JOBID*] [*-t HOSTS|RANKS*] [*-s SIGNUM*]


DESCRIPTION
===========

.. program:: flux housekeeping

The housekeeping service provides similar functionality to
a job epilog, with a few advantages

 - Housekeeping runs after the job, which is then allowed to exit CLEANUP
   state and become inactive once resources are released.
 - While housekeeping is running, the scheduler still thinks resources are
   allocated to the job, and will not allocate resources to other jobs.
 - Housekeeping supports partial release of resources back to the scheduler,
   such that a subset of stuck nodes do not hold up other nodes from
   being returned to service.

The :program:`flux housekeeping` command is used to interact with the
housekeeping service. It supports listing the resources currently executing
housekeeping actions and a command to forcibly terminate actions on a per-job
or per-node basis.


COMMANDS
========

list
----

.. program:: flux housekeeping list

:program:`flux housekeeping list` lists active housekeeping tasks by jobid.

.. option:: -i, --include=TARGETS

  Filter results to only include resources matching *TARGETS*, which may
  be specified either as an idset of broker ranks or a list of hosts in
  hostlist form. It is not an error to specify ranks or hosts that do not
  exist.

.. option:: -o, --format=FORMAT

  Customize the output format (See the `OUTPUT FORMAT`_ section below).

.. option:: -n, --no-header

  Suppress header from output.

kill
----

.. program:: flux housekeeping kill

:program:`flux housekeeping kill` can be used to terminate active housekeeping
tasks. Housekeeping may be terminated by jobid, a set of targets such as
broker ranks or hostnames, or all housekeeping may be terminated via the
:option:`--all` option.

.. option:: -s, --signal=SIGNUM

  Send signal SIGNUM instead of SIGTERM.

.. option:: -t, --targets=RANK|HOSTS

  Target a specific set of ranks or hosts.

.. option:: -j, --jobid=JOBID

  Target a specific job by JOBID. Without ``--targets`` this will kill all
  housekeeping tasks for the specified job.

.. option:: --all

  Target all housekeeping tasks for all jobs.

OUTPUT FORMAT
=============

The :option:`--format` option can be used to specify an output format using
Python's string format syntax or a defined format by name. For a list of
built-in and configured formats use :option:`-o help`.

The following field names can be specified for
:command:`flux housekeeping list`:

**id**
   The jobid that triggered housekeeping

**runtime**
   The time since this housekeeping task started

**nnodes**
   A synonym for **allocated.nnodes**

**ranks**
   A synonym for **allocated.ranks**

**nodelist**
   A synonym for **allocated.nodelist**

**allocated.nnodes**
   The number of nodes still allocated to this housekeeping task.

**allocated.ranks**
   The list of broker ranks still allocated to this housekeeping task.

**allocated.ranks**
   The list of nodes still allocated to this housekeeping task.

**pending.nnodes**
   The number of nodes that still need to complete housekeeping.

**pending.ranks**
   The list of broker ranks that still need to complete housekeeping.

**pending.ranks**
   The list of nodes that still need to complete housekeeping.

RESOURCES
=========

.. include:: common/resources.rst

SEE ALSO
========

:man5:`flux-config-job-manager`
