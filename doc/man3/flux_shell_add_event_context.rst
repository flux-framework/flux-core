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


DESCRIPTION
===========

Add extra context that will be emitted with shell standard event
``name`` using Jansson ``json_pack()`` style arguments. The ``flags``
parameter is currently unused.


RETURN VALUE
============

Returns 0 on success, -1 if ``shell``, ``name`` or ``fmt`` are NULL.


ERRORS
======

EINVAL
   ``shell``, ``name`` or ``fmt`` are NULL.


RESOURCES
=========

Flux: http://flux-framework.org
