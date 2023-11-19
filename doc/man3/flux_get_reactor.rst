===================
flux_get_reactor(3)
===================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

  #include <flux/core.h>

  flux_reactor_t *flux_get_reactor (flux_t *h);

  int flux_set_reactor (flux_t *h, flux_reactor_t *r);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_get_reactor` retrieves a :type:`flux_reactor_t` object previously
associated with the broker handle :var:`h` by a call to
:func:`flux_set_reactor`.  If one has not been previously associated,
a :type:`flux_reactor_t` object is created on demand. If the
:type:`flux_reactor_t` object is created on demand, it will be destroyed when
the handle is destroyed, otherwise it is the responsibility of the owner to
destroy it after the handle is destroyed.

:func:`flux_set_reactor` associates a :type:`flux_reactor_t` object :var:`r`
with a broker handle :var:`h`. A :type:`flux_reactor_t` object may be obtained
from another handle, for example when events from multiple handles are to be
managed using a common :type:`flux_reactor_t`, or one may be created directly
with :man3:`flux_reactor_create`. :func:`flux_set_reactor` should be called
immediately after :man3:`flux_open` to avoid conflict with other API calls
which may internally call :func:`flux_get_reactor`.


RETURN VALUE
============

:func:`flux_get_reactor` returns a :type:`flux_reactor_t` object on success.
On error, NULL is returned, and :var:`errno` is set appropriately.

:func:`flux_set_reactor` returns 0 on success, or -1 on failure with
:var:`errno` set appropriately.


ERRORS
======

ENOMEM
   Out of memory.

EEXIST
   Handle already has a reactor association.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man3:`flux_future_create`, :man3:`flux_reactor_destroy`
