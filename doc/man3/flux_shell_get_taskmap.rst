=========================
flux_shell_get_taskmap(3)
=========================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>
   #include <errno.h>

   const struct taskmap * flux_shell_get_taskmap (flux_shell_t *shell);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_shell_get_taskmap` returns the current shell task map. The
task map can be used to map job task ranks to node IDs and get the set
of tasks assigned to any node ID. The :type:`struct taskmap` object
can be queried via the functions exported in ``libflux-taskmap.so``.


RETURN VALUE
============

This function returns a pointer to the shell's internal :type:`struct taskmap`
or ``NULL`` on failure with :var:`errno` set.


ERRORS
======

EINVAL
   if :var:`shell` is NULL or the function is called before the initial taskmap
   is set by the shell.


RESOURCES
=========

.. include:: common/resources.rst

RFC 34 - Flux Task Map: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_34.html
