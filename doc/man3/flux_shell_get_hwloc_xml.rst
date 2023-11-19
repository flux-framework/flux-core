===========================
flux_shell_get_hwloc_xml(3)
===========================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>
   #include <errno.h>

   int flux_shell_get_hwloc_xml (flux_shell_t *shell,
                                 const char **hwloc_xml);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_shell_get_hwloc_xml` returns an hwloc XML string which has
been cached by the job shell. This XML string can be used to load an
hwloc topology via :func:`hwloc_topology_load` without requiring shell
components to rediscover the entire topology by probing the local
system. This can make loading hwloc topology much more efficient.

RETURN VALUE
============

:func:`flux_shell_get_hwloc_xml` returns 0 on success and -1 on error.


ERRORS
======

EINVAL
   :var:`shell` or :var:`hwloc_xml` are NULL, or the current :var:`shell`
   object is being used uninitialized.
    


RESOURCES
=========

.. include:: common/resources.rst
