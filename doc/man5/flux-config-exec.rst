===================
flux-config-exec(5)
===================


DESCRIPTION
===========

The Flux system instance **job-exec** service requires additional
configuration via the ``exec`` table, for example to enlist the services
of a setuid helper to launch jobs as guests.

The ``exec`` table may contain the following keys:


KEYS
====

imp
   (optional) Set the path to the IMP (Independent Minister of Privilege)
   helper program, as described in RFC 15, so that jobs may be launched with
   the credentials of the guest user that submitted them.  If unset, only
   jobs submitted by the instance owner may be executed.

service
   (optional) Set the remote subprocess service name. (Default: ``rexec``).
   Note that ``systemd.enable`` must be set to ``true`` if ``sdexec`` is
   configured.  See :man5:`flux-config-systemd`.

service-override
   (optional) Allow ``service`` to be overridden on a per-job basis with
   ``--setattr system.exec.bulkexec.service=NAME``.  (Default: ``false``).

method
   (optional) Run job shell under a specific mechanism other than the default
   forked subprocesses.  Potential configurations:

   systemd

   Run job shells are run under systemd, the job shell may be able to
   survive an unexpected broker shutdown and be recovered when the
   broker is restarted.

job-shell
   (optional) Override the compiled-in default job shell path.


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
