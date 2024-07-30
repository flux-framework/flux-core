.. flux-help-include: true
.. flux-help-section: jobs
.. flux-help-command: pgrep/pkill

==============
flux-pgrep(1)
==============


SYNOPSIS
========

**flux** **pgrep** [*OPTIONS*] expression..

**flux** **pkill** [*OPTIONS*] expression..

DESCRIPTION
===========

.. program:: flux pgrep

:program:`flux pgrep` lists jobids that match a supplied expression. The
expression may contain a pattern which matches the job name, or
a range of jobids in the form ``jobid1..jobid2``. If both a pattern
and jobid range are supplied then both must match.

In rare cases, a pattern may appear to be a jobid range, when the
pattern ``..`` is used and the strings on both sides area also valid
Flux jobids. In that case, a name pattern match can be forced by
prefixing the pattern with ``name:``, e.g. ``name:fr..123``.

By default, only active jobs for the current user are considered.

:program:`flux pkill` cancels matching jobs instead of listing them.

OPTIONS
=======

.. option:: -a

   Include jobs in all states, including inactive jobs.
   This is shorthand for :option:`--filter=pending,running,inactive`.
   (pgrep only)

.. option:: -A

   Include jobs for all users. This is shorthand for :option:`--user=-all`.

.. option:: -u, --user=USER

   Fetch jobs only for the given user, instead of the current UID.

.. option:: -f, --filter=STATE|RESULT

   Include jobs with specific job state or result. Multiple states or
   results can be listed separated by comma. See the JOB STATUS section
   of the :man1:`flux-jobs` manual for more detail.

.. option:: -q, --queue=QUEUE[,...]

   Only include jobs in the named queue *QUEUE*. Multiple queues may be
   specified as a comma-separated list, or by using the :option:`--queue`
   option multiple times.

.. option:: -c, --count=N

   Limit output to the first *N* matches (default 1000).

.. option:: --max-entries=N

   Limit the number of jobs to consider to *N* entries (default 1000).

.. option:: -o, --format=NAME|FORMAT

   Specify a named output format *NAME* or a format string using Python's
   format syntax.  An alternate default format can be set via the
   :envvar:`FLUX_PGREP_FORMAT_DEFAULT` environment variable.  For full
   documentation of this option, including supported field names and format
   configuration options, see :man1:flux-jobs. This command shares configured
   named formats with *flux-jobs* by reading *flux-jobs* configuration files.
   Supported builtin named formats include *default*, *full*, *long*, and
   *deps*. The default format emits the matched jobids only. (pgrep only)

.. option:: -n, --no-header

   Suppress printing of the header line. (pgrep only)

.. option:: -w, --wait

   Wait for jobs to finish after cancel. (pkill only)

EXIT STATUS
===========

0
   One or more jobs matched the supplied expression. For *pkill* the
   process have also been successfully canceled.

1
   No jobs matched or there was an error canceling them.

2
   Syntax or other command line error.

RESOURCES
=========

.. include:: common/resources.rst

SEE ALSO
========

:man1:`flux-jobs`
