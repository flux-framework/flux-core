.. flux-help-section: jobs

==============
flux-update(1)
==============

SYNOPSIS
========

**flux** **update** [*OPTIONS*] JOBID KEY=VALUE [KEY=VALUE...]

DESCRIPTION
===========

flux-update(1) requests an update of one or more attributes for an active
(pending or running) job. Updates are permitted and validated by the job
manager before being applied to the job.

Keys are expressed as period-delimited strings corresponding to an attribute
path in jobspec. If a key does not start with ``attributes.``, ``tasks.``,
or ``.resources`` (the top-level jobspec keys allowed by RFC 14), then
the key is assumed to be prefixed with ``attributes.system.``, such that::

  $ flux update f12345 myattr="value"

would request an update of ``attributes.system.myattr`` to the string value
``"value"``.

The flux-update(1) command may also support other convenient key aliases.
Key aliases are listed in the SPECIAL KEYS section below.

Updates will be sent to the job manager update service, which checks that
the current user is permitted to apply all updates, and that all updates
are valid. If multiple updates are specified on the command line, then
all updates are either applied or the request fails.

.. note::
   Job updates are allowed in the job manager by special plugins on
   a case by case basis. At this time, the set of keys that can actually
   be updated for a job may be very limited.

The flux-update(1) command may also support special handling of values
for specific keys. Those special cases are documented in the SPECIAL KEYS
section below.

OPTIONS
=======

**-n, --dry-run**
  Do not send update to job manager, but print the updates in JSON to
  stdout.

**-v, --verbose**
  Print updated keys on success.

SPECIAL KEYS
============

*attributes.system.duration*, *duration*
  Updates of the job ``duration`` can take the form of of *[+-]FSD*, where
  ``+`` or ``-`` indicate an adjustment of the existing duration, and *FSD*
  is any string or number in RFC 23 Flux Standard Duration. Examples include
  ``60``, ``1m``, ``1.5h``, ``+10m``, ``-1h``.

*name*
  Alias for job name, i.e. ``attributes.system.job.name``

EXIT STATUS
===========

0
  All updates were successful

1
  Updates were invalid or not permitted, or the provided JOBID was invalid
  or inactive, or the user does not have permission to modify the job

2
  Syntax or other command line error

RESOURCES
=========

Flux: http://flux-framework.org

RFC 14: Canonical Job Specification: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_14.html

SEE ALSO
========

:man1:`flux-jobs`, :man1:`flux-submit`, :man1:`flux-bulksubmit`
