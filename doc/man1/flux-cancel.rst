.. flux-help-description: cancel one or more jobs
.. flux-help-section: jobs

==============
flux-cancel(1)
==============


SYNOPSIS
========

**flux** **cancel** [*OPTIONS*] [*JOBID...*]

DESCRIPTION
===========

.. program:: flux cancel

:program:`flux cancel` cancels one or more jobs by raising a job exception of
type=cancel. An optional message included with the cancel exception may be
provided via the :option:`--message` option. Canceled jobs are immediately
sent SIGTERM followed by SIGKILL after a configurable timeout (default=5s).

:program:`flux cancel` can target multiple jobids by either taking them on the
command line, or via the selection options :option:`--all`, :option:`--user`,
or :option:`--states`. It is an error to provide jobids on the command line
and use one or more of the selection options.

By default :option:`--all` will target all jobs for the current user. To
target all jobs for all users, use :option:`--user=all` (only the instance
owner is allowed to use :option:`--user=all`). To see how many jobs
:program:`flux cancel` would kill, use the :option:`--dry-run` option.

OPTIONS
=======

.. option:: -n, --dry-run

   Do not cancel any jobs, but print a message indicating how many jobs
   would have been canceled.

.. option:: -m, --message=NOTE

   Set an optional exception note.

.. option:: -u, --user=USER

   Set target user.  The instance owner may specify *all* for all users.

.. option:: -S, --states=STATES

   Set target job states (default: active). Valid states include
   depend, priority, sched, run, pending, running, active.

.. option:: -q, --quiet

   Suppress output if no jobs match

RESOURCES
=========

.. include:: common/resources.rst

