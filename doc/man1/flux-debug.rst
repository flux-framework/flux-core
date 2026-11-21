=============
flux-debug(1)
=============


SYNOPSIS
========

**flux** **debug** **alloc-check** [*-n*] [*-v*] [*-o DIR*] [*--color[=WHEN]*]


DESCRIPTION
===========

.. program:: flux debug

:program:`flux debug` provides subcommands for debugging specific conditions
in a Flux instance, typically conditions caused by bugs that may be present
in some releases and fixed in others.

Because they target conditions that may come and go, subcommands may be added
or removed in any release. Do not depend on them in scripts.

All subcommands write their output to standard error.


COMMON OPTIONS
==============

The following options are supported by all subcommands:

.. option:: -v, --verbose

  Log what the command is doing.

.. option:: --color[=WHEN]

  Decorate output with color and status symbols. *WHEN* can be ``never``,
  ``always``, or ``auto``. The default is ``auto``, which enables color
  when standard error is a terminal and the :envvar:`NO_COLOR` environment
  variable is not set. If *WHEN* is omitted, ``always`` is assumed.


COMMANDS
========

alloc-check
-----------

.. program:: flux debug alloc-check

:program:`flux debug alloc-check` checks the resources allocated in a Flux
instance for consistency. It is intended to help diagnose scheduler or
job-manager bugs that result in resources being allocated to more than one
job at a time, or in the job-manager and scheduler disagreeing on which
resources are allocated.

Two checks are performed:

overlapping allocations
  The resources held by every running job of every user, plus resources
  still held by housekeeping (see :man1:`flux-housekeeping`), are compared
  pairwise and every pair that overlaps is reported along with the
  overlapping resources and relevant event timestamps for each holder.
  This is the same set of allocations the job-manager combines to report
  allocated resources, so jobs that have already released their resources
  and jobs using the alloc-bypass jobtap plugin are skipped.

  When the job-manager detects overlap, it only reports a single
  ``duplicate allocation`` error naming whichever job it happened to
  encounter first, which may not be the job that was double booked.
  :program:`flux debug alloc-check` reports all conflicting pairs instead.

job-manager and scheduler agreement
  The set of resources the job-manager considers allocated is compared with
  the set the scheduler considers allocated, and any resources allocated in
  one view but not the other are reported. For resources allocated only in
  the job-manager view, the jobs holding them are listed. Allocations and
  frees in flight between the job-manager and scheduler cause brief
  differences, so when the views differ, both are queried again up to five
  times, one second apart, and only differences that persist are reported.
  If the scheduler does not support this query, a warning is printed and
  the comparison is skipped.

When overlapping allocations are found, a debug bundle is collected unless
:option:`--no-collect` is used. See `DEBUG BUNDLE`_ below.

The command must be run by the instance owner (e.g. the ``flux`` user or
root in a system instance), since it needs access to the jobs of all users.
If permission is denied, it exits with an error rather than checking a
partial view.

With the common :option:`--verbose <flux debug --verbose>` option, the number of jobs and housekeeping
allocations found, each job skipped and why, each query of the job-manager
and scheduler, and each command run while collecting the debug bundle are
logged.

.. option:: -o, --outdir=DIR

  Write the debug bundle to directory *DIR* and the tarball *DIR.tar.gz*.
  The default is :file:`flux-alloc-check-YYYYMMDD-HHMMSS` in the current
  directory.

.. option:: -n, --no-collect

  Report problems only, do not collect a debug bundle.


DEBUG BUNDLE
============

The :program:`flux debug alloc-check` debug bundle is a directory, along
with a compressed tarball of that directory suitable for attaching to a bug
report, containing:

:file:`summary.json`
  The conflicts found, the allocations checked, jobs skipped and why, and
  any job-manager and scheduler disagreement.

command output
  Text files containing the output of commands that describe the instance,
  including :man1:`flux-dmesg`, :man1:`flux-resource` list, status, R, and
  eventlog, :man1:`flux-housekeeping` list, :man1:`flux-queue` status,
  :man1:`flux-jobtap` list, :man1:`flux-config` get, :man1:`flux-jobs`, and
  scheduler module statistics. Each file begins with the command that
  produced it.

:file:`job-manager-stats.json`, :file:`job-manager.resource-status.json`, :file:`sched.resource-status.json`
  Raw responses from the job-manager and scheduler.

:file:`jobs/JOBID/`
  For each job involved in a conflict, the job's :man1:`flux-jobs` long
  listing, R, jobspec, main and exec eventlogs, and when the scheduler is
  Fluxion, the resources Fluxion has allocated to the job.


EXIT STATUS
===========

0
  No problems were found.

1
  The condition checked for was found, or an error occurred.

2
  Invalid option specified or other command line error.


CAVEATS
=======

:program:`flux debug alloc-check` gathers allocations with several requests
to different services while the instance is running. A job that starts or
completes during the check may therefore be missed or skipped. Run the
command again to confirm a problem.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man1:`flux-resource`, :man1:`flux-housekeeping`, :man1:`flux-jobs`,
:man1:`flux-dmesg`
