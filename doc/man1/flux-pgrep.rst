.. flux-help-include: true

==============
flux-pgrep(1)
==============


SYNOPSIS
========

**flux** **pgrep** [*OPTIONS*] expression..

**flux** **pkill** [*OPTIONS*] expression..

DESCRIPTION
===========

*flux-pgrep* lists jobids that match a supplied expression. The
expression may contain a pattern which matches the job name, or
a range of jobids in the form ``jobid1..jobid2``. If both a pattern
and jobid range are supplied then both must match.

In rare cases, a pattern may appear to be a jobid range, when the
pattern ``..`` is used and the strings on both sides area also valid
Flux jobids. In that case, a name pattern match can be forced by
prefixing the pattern with ``name:``, e.g. ``name:fr..123``.

By default, only active jobs for the current user are considered.

*flux-pkill* cancels matching jobs instead of listing them. This is
equivalent to running *flux-pgrep* with the ``-k`` option.

OPTIONS
=======

**-a**
   Include jobs in all states, including inactive jobs.
   This is shorthand for *--filter=pending,running,inactive*.
   (pgrep only)

**-A**
   Include jobs for all users. This is shorthand for *--user=-all*.

**-u, --user**\ *=USER*
   Fetch jobs only for the given user, instead of the current UID.

**-f, --filter**\ *=STATE|RESULT*
   Include jobs with specific job state or result. Multiple states or
   results can be listed separated by comma. See the JOB STATUS section
   of the :man1:`flux-jobs` manual for more detail.

**-q, --queue**\ *=QUEUE*
   Only include jobs in the named queue *QUEUE*.

**-c, --count**\ *=N*
   Limit output to the first *N* matches (default 1000).

**--max-entries**\ *=N*
   Limit the number of jobs to consider to *N* entries (default 1000).

**-o, --format**\ *=NAME|FORMAT*
   Specify a named output format *NAME* or a format string using Python's
   format syntax. For full documentation of this option, including supported
   field names and format configuration options, see :man1:flux-jobs. This
   command shares configured named formats with *flux-jobs* by reading
   *flux-jobs* configuration files. Supported builtin named formats include
   *default*, *full*, *long*, and *deps*. The default format emits the matched
   jobids only. (pgrep only)

**-n, --no-header**
   Suppress printing of the header line. (pgrep only)

**-w, --wait**
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

Flux: http://flux-framework.org

SEE ALSO
========

:man1:`flux-jobs`
