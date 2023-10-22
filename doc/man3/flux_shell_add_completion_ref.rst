================================
flux_shell_add_completion_ref(3)
================================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>
   #include <errno.h>

   int flux_shell_add_completion_ref (flux_shell_t *shell,
                                      const char *fmt,
                                      ...)

   int flux_shell_remove_completion_ref (flux_shell_t *shell,
                                         const char *fmt,
                                         ...)


DESCRIPTION
===========

``flux_shell_add_completion_ref`` creates a named "completion
reference" on the shell object ``shell`` so that the shell will
not consider a job "complete" until the reference is released with
``flux_shell_remove_completion_ref``. Once all references have been
removed, the shells reactor is stopped with
``flux_reactor_stop(shell->r)``.


RETURN VALUE
============

``flux_shell_add_completion_ref`` returns the reference count for the
particular name, or -1 on error.

``flux_shell_remove_completion_ref`` returns 0 on success, -1 on failure.


ERRORS
======

EINVAL
   Either ``shell`` or ``fmt`` are NULL.


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man3:`flux_reactor_stop`
