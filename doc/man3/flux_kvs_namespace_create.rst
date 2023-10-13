============================
flux_kvs_namespace_create(3)
============================


SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   flux_future_t *flux_kvs_namespace_create (flux_t *h,
                                             const char *namespace,
                                             uint32_t owner,
                                             int flags);

   flux_future_t *flux_kvs_namespace_create_with (flux_t *h,
                                                  const char *namespace,
                                                  const char *rootref,
                                                  uint32_t owner,
                                                  int flags);

   flux_future_t *flux_kvs_namespace_remove (flux_t *h,
                                             const char *namespace);


DESCRIPTION
===========

``flux_kvs_namespace_create()`` creates a KVS namespace. Within a
namespace, users can get/put KVS values completely independent of
other KVS namespaces. An owner of the namespace other than the
instance owner can be chosen by setting *owner*. Otherwise, *owner*
can be set to FLUX_USERID_UNKNOWN.

``flux_kvs_namespace_create_with()`` is identical to
``flux_kvs_namespace_create()`` but will initialize the namespace to
the specified *rootref*.  This may be useful in several circumstances,
such as initializing a namespace to an earlier checkpoint.

``flux_kvs_namespace_remove()`` removes a KVS namespace.


FLAGS
=====

The *flags* mask is currently unused and should be set to 0.


RETURN VALUE
============

``flux_kvs_namespace_create()`` and ``flux_kvs_namespace_remove()`` return
a ``flux_future_t`` on success, or NULL on failure with errno set
appropriately.


ERRORS
======

EINVAL
   One of the arguments was invalid.

ENOMEM
   Out of memory.

EPROTO
   A request was malformed.

ENOSYS
   The KVS module is not loaded.

EEXIST
   The namespace already exists.

ENOTSUP
   Attempt to remove illegal namespace.


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man3:`flux_kvs_lookup`, :man3:`flux_kvs_commit`
