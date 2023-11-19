===============================
flux_shell_add_event_context(3)
===============================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>
   #include <errno.h>

   int flux_shell_add_event_context (flux_shell_t *shell,
                                     const char *name,
                                     int flags,
                                     const char *fmt,
                                     ...);
Link with :command:`-lflux-core`.

DESCRIPTION
===========

Add extra context that will be emitted with shell standard event
:var:`name` using Jansson :func:`json_pack` style arguments. The :var:`flags`
parameter is currently unused.


RETURN VALUE
============

Returns 0 on success, -1 if :var:`shell`, :var:`name` or :var:`fmt` are NULL.


ERRORS
======

EINVAL
   :var:`shell`, :var:`name` or :var:`fmt` are NULL.


RESOURCES
=========

Flux: http://flux-framework.org
