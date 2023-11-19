================
flux_kvs_copy(3)
================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   flux_future_t *flux_kvs_copy (flux_t *h,
                                 const char *srckey,
                                 const char *dstkey,
                                 int commit_flags);

   flux_future_t *flux_kvs_move (flux_t *h,
                                 const char *srckey,
                                 const char *dstkey,
                                 int commit_flags);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_kvs_copy` sends a request via handle :var:`h` to the KVS service
to look up the directory entry of :var:`srckey`. Upon receipt of the response,
it then sends another request to commit a duplicate at :var:`dstkey`.
:var:`commit_flags` are passed through to the commit operation.
See the FLAGS section of :man3:`flux_kvs_commit`.

The net effect is that all content below :var:`srckey` is copied to
:var:`dstkey`.  Due to the hash tree organization of the KVS name space, only
the directory entry needs to be duplicated to create a new, fully independent
deep copy of the original data.

:func:`flux_kvs_move` first performs a :func:`flux_kvs_copy`, then sends a
commit request to unlink :var:`srckey`. :var:`commit_flags` are passed through
to the commit within :func:`flux_kvs_copy`, and to the commit which performs
the unlink.

:func:`flux_kvs_copy` and :func:`flux_kvs_move` are capable of working across
namespaces. See :man3:`flux_kvs_commit` for info on how to select a
namespace other than the default.


CAVEATS
=======

:func:`flux_kvs_copy` and :func:`flux_kvs_commit` are implemented as aggregates
of multiple KVS operations. As such they do not have the "all or nothing"
guarantee of a being carried out within a single KVS transaction.

In the unlikely event that the copy phase of a :func:`flux_kvs_move`
succeeds but the unlink phase fails, :func:`flux_kvs_move` may return failure
without cleaning up the new copy. Since the copy phase already validated
that the unlink target key exists by copying from it, the source of such a
failure would be a transient error such as out of memory or communication
failure.


RETURN VALUE
============

:func:`flux_kvs_copy` and :func:`flux_kvs_move` return a :type:`flux_future_t`
on success, or NULL on failure with errno set appropriately.


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

ENOTSUP
   An unknown namespace was requested.


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man3:`flux_future_get`, :man3:`flux_kvs_commit`
