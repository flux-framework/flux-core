==================
flux_kvs_commit(3)
==================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   flux_future_t *flux_kvs_commit (flux_t *h,
                                   const char *ns,
                                   int flags,
                                   flux_kvs_txn_t *txn);

   flux_future_t *flux_kvs_fence (flux_t *h,
                                  const char *ns,
                                  int flags,
                                  const char *name,
                                  int nprocs,
                                  flux_kvs_txn_t *txn);

   int flux_kvs_commit_get_treeobj (flux_future_t *f,
                                    const char **treeobj);

   int flux_kvs_commit_get_sequence (flux_future_t *f, int *seq);


DESCRIPTION
===========

``flux_kvs_commit()`` sends a request via handle *h* to the KVS service
to commit a transaction *txn*. *txn* is created with
:man3:`flux_kvs_txn_create` and after commit completion, is destroyed
with :man3:`flux_kvs_txn_destroy`. A ``flux_future_t`` object is returned,
which acts as handle for synchronization and container for the
response. The *txn* will operate in the namespace specified by *ns*.
If *ns* is NULL, ``flux_kvs_commit()`` will operate on the default
namespace, or if set, the namespace from the FLUX_KVS_NAMESPACE
environment variable. Note that all transactions operate on the same
namespace.

``flux_kvs_fence()`` is a "collective" version of ``flux_kvs_commit()`` that
supports multiple callers. Each caller uses the same *flags*, *name*,
and *nprocs* arguments. Once *nprocs* requests are received by the KVS
service for the named operation, the transactions are combined and committed
together as one transaction. *name* must be unique across the Flux session
and should not be reused, even after the fence is complete.

:man3:`flux_future_then` may be used to register a reactor callback
(continuation) to be called once the response to the commit/fence
request has been received. :man3:`flux_future_wait_for` may be used to
block until the response has been received. Both accept an optional timeout.

:man3:`flux_future_get`, ``flux_kvs_commit_get_treeobj()``, or
``flux_kvs_commit_get_sequence()`` can decode the response. A return of
0 indicates success and the entire transaction was committed. A
return of -1 indicates failure, none of the transaction was committed.
All can be used on the ``flux_future_t`` returned by ``flux_kvs_commit()``
or ``flux_kvs_fence()``.

In addition to checking for success or failure,
``flux_kvs_commit_get_treeobj()`` and ``flux_kvs_commit_get_sequence()``
can return information about the root snapshot that the commit or
fence has completed its transaction on.

``flux_kvs_commit_get_treeobj()`` obtains the root hash in the form of
an RFC 11 *dirref* treeobj, suitable to be passed to
``flux_kvs_lookupat(3)``.

``flux_kvs_commit_get_sequence()`` retrieves the monotonic sequence number
for the root.


FLAGS
=====

The following are valid bits in a *flags* mask passed as an argument
to ``flux_kvs_commit()`` or ``flux_kvs_fence()``.

FLUX_KVS_NO_MERGE
   The KVS service may merge contemporaneous commit transactions as an
   optimization. However, if the combined transactions modify the same key,
   a watch on that key may only be notified of the last-in value. This flag
   can be used to disable that optimization for this transaction.


RETURN VALUE
============

``flux_kvs_commit()`` and ``flux_kvs_fence()`` return a ``flux_future_t`` on
success, or NULL on failure with errno set appropriately.


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

EOVERFLOW
   ``flux_kvs_fence()`` has been called too many times and *nprocs* has
   been exceeded.


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man3:`flux_future_get`, :man3:`flux_kvs_txn_create`
