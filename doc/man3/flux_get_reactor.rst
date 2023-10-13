===================
flux_get_reactor(3)
===================


SYNOPSIS
========

.. code-block:: c

  #include <flux/core.h>

  flux_reactor_t *flux_get_reactor (flux_t *h);

  int flux_set_reactor (flux_t *h, flux_reactor_t *r);


DESCRIPTION
===========

``flux_get_reactor()`` retrieves a flux_reactor_t object previously
associated with the broker handle *h* by a call to ``flux_set_reactor()``.
If one has not been previously associated, a flux_reactor_t object is created
on demand. If the flux_reactor_t object is created on demand, it will be
destroyed when the handle is destroyed, otherwise it is the responsibility
of the owner to destroy it after the handle is destroyed.

``flux_set_reactor()`` associates a flux_reactor_t object *r* with a broker
handle *h*. A flux_reactor_t object may be obtained from another handle,
for example when events from multiple handles are to be managed using
a common flux_reactor_t, or one may be created directly with
:man3:`flux_reactor_create`. ``flux_set_reactor()`` should be called
immediately after :man3:`flux_open` to avoid conflict with other API calls
which may internally call ``flux_get_reactor()``.


RETURN VALUE
============

``flux_get_reactor()`` returns a flux_reactor_t object on success.
On error, NULL is returned, and errno is set appropriately.

``flux_set_reactor()`` returns 0 on success, or -1 on failure with
errno set appropriately.


ERRORS
======

ENOMEM
   Out of memory.

EEXIST
   Handle already has a reactor association.


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man3:`flux_future_create`, :man3:`flux_reactor_destroy`
