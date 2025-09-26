==========================
flux_msg_handler_addvec(3)
==========================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   struct flux_msg_handler_spec {
       int typemask;
       const char *topic_glob;
       flux_msg_handler_f cb;
       uint32_t rolemask;
   };

   int flux_msg_handler_addvec (flux_t *h,
                                const struct flux_msg_handler_spec tab[],
                                void *arg,
                                flux_msg_handler_t **handlers[]);

   void flux_msg_handler_delvec (flux_msg_handler_t *handlers[]);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_msg_handler_addvec` creates and starts an array of message handlers,
terminated by FLUX_MSGHANDLER_TABLE_END. The new message handler objects
are assigned to an internally allocated array, returned in :var:`handlers`.
The last entry in the array is set to NULL.

:func:`flux_msg_handler_delvec` stops and destroys an array of message handlers
returned from :func:`flux_msg_handler_addvec`.

These functions are convenience functions which call
:man3:`flux_msg_handler_create`, :man3:`flux_msg_handler_start`,
:man3:`flux_msg_handler_allow_rolemask`; and :man3:`flux_msg_handler_stop`,
:man3:`flux_msg_handler_destroy` on each element of the array, respectively.
The defaults of :var:`typemask` and :var:`topic_glob` are described in
:man3:`flux_msg_cmp`.  The default rolemask is described in
:man3:`flux_msg_handler_allow_rolemask` and is set with a :var:`rolemask` of 0.

If :func:`flux_msg_handler_addvec` encounters an error creating a message
handler, all previously created message handlers in the array are destroyed
before an error is returned.


RETURN VALUE
============

:func:`flux_msg_handler_addvec` returns zero on success.
On error, -1 is returned, and :var:`errno` is set appropriately.


ERRORS
======

ENOMEM
   Out of memory.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man3:`flux_msg_handler_create`, :man3:`flux_msg_handler_allow_rolemask`
