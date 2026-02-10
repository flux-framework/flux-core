================
flux_attr_get(3)
================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   const char *flux_attr_get (flux_t *h, const char *name);

   int flux_attr_set (flux_t *h, const char *name, const char *val);

   int flux_attr_set_ex (flux_t *h,
                         const char *name,
                         const char *val,
                         bool force,
                         flux_error_t *error);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

Flux broker attributes are both a simple, general-purpose key-value
store with scope limited to the local broker rank, and a method for the
broker to export information needed by Flux services and utilities.

:func:`flux_attr_get` retrieves the value of the attribute :var:`name`.

Attributes that have the broker tags as *immutable* are cached automatically
in the :type:`flux_t` handle. For example, the "rank" attribute is a frequently
accessed attribute whose value never changes. It will be cached on the first
access and thereafter does not require an RPC to the broker to access.

:func:`flux_attr_set` updates the value of attribute :var:`name` to :var:`val`.
If :var:`name` is not currently a valid attribute, it is created.
If :var:`val` is NULL, the attribute is unset.

:func:`flux_attr_set_ex` is the same as above with extra arguments.  If
:var:`force` is set, protections against runtime updates of the attribute
are bypassed.  If :var:`error` is non-NULL, it is filled with a human
readable error message on failure.


RETURN VALUE
============

:func:`flux_attr_get` returns the requested value on success. On error, NULL
is returned and :var:`errno` is set appropriately.

:func:`flux_attr_set` and :func:`flux_attr_set_ex` return zero on success.
On error, -1 is returned and errno is set appropriately.


ERRORS
======

ENOENT
   The requested attribute is invalid or has a NULL value on the broker.

EINVAL
   Some arguments were invalid.

EPERM
   Set was attempted on an attribute that is not writable with the
   user's credentials.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man1:`flux-getattr`, :man7:`flux-broker-attributes`,
