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
