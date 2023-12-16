==========================
flux_msg_create(3)
==========================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   flux_msg_t *flux_msg_create (int type)

   flux_msg_t *flux_msg_copy (const flux_msg_t *msg, bool payload)

   const flux_msg_t *flux_msg_incref (const flux_msg_t *msg)
  
   void flux_msg_decref (const flux_msg_t *msg)

   void flux_msg_destroy (flux_msg_t *msg)


DESCRIPTION
===========

:func:`flux_msg_create` creates a :type:`flux_msg_t` of :var:`type`.
Different types of Flux messages are defined in RFC :doc:`rfc:spec_3`. All
messages have a starting reference count of 1.

:func:`flux_msg_copy` duplicates :var:`msg`. The payload is omitted unless
:var:`payload` is true. The initial reference count of the new message is 1.

:func:`flux_msg_incref` increments the reference count of :var:`msg`
by 1.

:func:`flux_msg_decref` decrements the reference count of :var:`msg`
by 1. When the reference count reaches 0, the message is destroyed.

:func:`flux_msg_destroy` is an alias for :func:`flux_msg_decref`.

RETURN VALUE
============

:func:`flux_msg_create` and :func:`flux_msg_copy` return a 
:type:`flux_msg_t` type on success. On failure, NULL is returned and
:var:`errno` is set.

:func:`flux_msg_incref` returns a constant pointer to :var:`msg` for
convenience. On failure, NULL is returned and :var:`errno` is set.

:func:`flux_msg_decref` and :func:`flux_msg_destroy` have no return value.

ERRORS
======

ENOMEM
   Out of memory.

EINVAL
   Invalid message or message type.

RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man3:`flux_send`, :man3:`flux_respond`
