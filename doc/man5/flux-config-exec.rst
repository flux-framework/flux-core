===================
flux-config-exec(5)
===================


DESCRIPTION
===========

The Flux system instance **job-exec** service requires additional
configuration via the ``exec`` table, for example to enlist the services
of a setuid helper to launch jobs as guests.  Additional configuration
options may exist under the ``exec.<method>`` depending on the setting
of ``method`` below.


EXEC KEYS
=========

The ``exec`` table may contain the following keys:

imp
   (optional) Set the path to the IMP (Independent Minister of Privilege)
   helper program, as described in RFC 15, so that jobs may be launched with
   the credentials of the guest user that submitted them.  If unset, only
   jobs submitted by the instance owner may be executed.

method
   (optional) Run job shell under a specific mechanism other than the default
   forked subprocesses.  Potential configurations:

   systemd

   Run job shells are run under systemd, the job shell may be able to
   survive an unexpected broker shutdown and be recovered when the
   broker is restarted.

job-shell
   (optional) Override the compiled-in default job shell path.


EXEC.SYSTEMD KEYS
=================

The ``exec.systemd`` table may contain the following keys.  These are only valid
if ``method`` is set to ``systemd``.

cpu_set_affinity

   (optional) Set to true to set CPU affinity to only allocated cores.

cpu_set_allowed

   (optional) Set to true to set limit execution to only allocated cores.

MemoryHigh

   (optional) Set to number of bytes memory high limit should be.
   Memory usage can go above this limit but can be throttled.  The
   suffixes k, m, g, and t can be used for kilobytes, megabytes,
   gigabytes, and terabytes respectively.

   Optionally, a percentage of memory can also be configured via a
   number between 0 and 100 followed by a '%' sign.

   The special value "infinity" can also be specified to indicate
   100%.

MemoryMax

   (optional) Set to number of bytes memory max should be.  Memory
   cannot go above this value, otherwise an out-of-memory failure will
   occur.  The suffixes k, m, g, and t can be used for kilobytes,
   megabytes, gigabytes, and terabytes respectively.

   Optionally, a percentage of memory can also be configured via a
   number between 0 and 100 followed by a '%' sign.

   The special value "infinity" can also be specified to indicate
   100%.

EXAMPLE
=======

::

   [exec]
   imp = "/usr/libexec/flux/flux-imp"
   job-shell = "/usr/libexec/flux/flux-shell-special"


RESOURCES
=========

Flux: http://flux-framework.org

RFC 15: Flux Security: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_15.html


SEE ALSO
========

:man5:`flux-config`
