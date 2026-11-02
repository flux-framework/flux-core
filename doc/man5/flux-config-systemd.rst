======================
flux-config-systemd(5)
======================


DESCRIPTION
===========

Flux can optionally launch jobs using systemd when :man5:`flux-config-exec`
selects the ``sdexec`` service.  The ``systemd`` table must also be configured.

KEYS
====

enable (optional)
   Boolean value enables the ``sdbus`` and ``sdexec`` modules to be loaded.
   (Default: ``false``).

sdbus-debug (optional)
   Boolean value enables debug logging by the ``sdbus`` module.

sdexec-debug (optional)
   Boolean value enables debug logging by the ``sdexec`` module.

sdexec-record-dir (optional)
   Directory in which the ``sdexec`` module keeps a record of each
   transient unit it starts, so that units left running or finished
   across a module or broker restart can be reattached and their exit
   status collected.  Records are small eventlog files named after the
   unit.  The directory must exist and be writable by the instance owner:
   if a record cannot be written, the unit is not started.
   (Default: the value of ``XDG_RUNTIME_DIR``, the runtime directory of
   the systemd user instance that owns the units, which is cleared on
   reboot.  If that is unset too, records are disabled and a status not
   collected before a restart is lost.)


EXAMPLE
=======

::

   [systemd]
   enable = true
   sdbus-debug = true
   sdexec-debug = true


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man5:`flux-config`
