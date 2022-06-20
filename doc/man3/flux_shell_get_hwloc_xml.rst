===========================
flux_shell_get_hwloc_xml(3)
===========================


SYNOPSIS
========

::

   #include <flux/shell.h>
   #include <errno.h>

::

   int flux_shell_get_hwloc_xml (flux_shell_t *shell,
                                 const char **hwloc_xml);


DESCRIPTION
===========

``flux_shell_get_hwloc_xml()`` returns an hwloc XML string which has
been cached by the job shell. This XML string can be used to load an
hwloc topology via ``hwloc_topology_load()`` without requiring shell
components to rediscover the entire topology by probing the local
system. This can make loading hwloc topology much more efficient.

RETURN VALUE
============

``flux_shell_get_hwloc_xml()`` returns 0 on success and -1 on error.


ERRORS
======

EINVAL
   ``shell`` or ``hwloc_xml`` are NULL, or the current ``shell`` object
   is being used uninitialized.
    


RESOURCES
=========

Flux: http://flux-framework.org

Jansson: https://jansson.readthedocs.io/en/2.10/apiref.html
