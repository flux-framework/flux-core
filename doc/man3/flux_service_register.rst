============================
flux_service_register(3)
============================


SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   flux_future_t *flux_service_register (flux_t *h, const char *name);

   flux_future_t *flux_service_unregister (flux_t *h, const char *name);


DESCRIPTION
===========

``flux_service_register()`` enables a new service *name* to be registered
with the flux broker.  On success, request message sent to "name.*" will
be routed to this handle until ``flux_service_unregister()`` is called
for *name*.

While ``flux_service_register()`` registers *name*, the user must
still setup a handler for the service.  One can be setup through
``flux_msg_handler_addvec(3)``.


RETURN VALUE
============

``flux_service_register()`` and ``flux_service_unregister()`` return a
``flux_future_t`` on success, or NULL on failure with errno set
appropriately.


ERRORS
======

EINVAL
   One of the arguments was invalid.

ENOMEM
   Out of memory.


RESOURCES
=========

Github: http://github.com/flux-framework


SEE ALSO
========

flux_future_get(3), flux_msg_handler_addvec(3)

