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

RFC 15: Flux Security: https://github.com/flux-framework/rfc/blob/master/spec_15.rs


SEE ALSO
========

:man5:`flux-config`
