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

   int flux_kvs_commit_get_treeobj (flux_future_t *f,
                                    const char **treeobj);

   int flux_kvs_commit_get_rootref (flux_future_t *f,
                                    const char **rootref);

   int flux_kvs_commit_get_sequence (flux_future_t *f, int *seq);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_kvs_commit` sends a request via handle :var:`h` to the KVS service
to commit a transaction :var:`txn`. :var:`txn` is created with
:man3:`flux_kvs_txn_create` and after commit completion, is destroyed
with :man3:`flux_kvs_txn_destroy`. A :type:`flux_future_t` object is returned,
which acts as handle for synchronization and container for the
response. The :var:`txn` will operate in the namespace specified by :var:`ns`.
If :var:`ns` is NULL, :func:`flux_kvs_commit` will operate on the default
namespace, or if set, the namespace from the FLUX_KVS_NAMESPACE
environment variable. Note that all transactions operate on the same
namespace.

:man3:`flux_future_then` may be used to register a reactor callback
(continuation) to be called once the response to the commit
request has been received. :man3:`flux_future_wait_for` may be used to
block until the response has been received. Both accept an optional timeout.

:man3:`flux_future_get`, :func:`flux_kvs_commit_get_treeobj`,
:func:`flux_kvs_commit_get_rootref`, or :func:`flux_kvs_commit_get_sequence`
can decode the response. A return of 0 indicates success and the entire
transaction was committed. A return of -1 indicates failure, none of the
transaction was committed.

In addition to checking for success or failure,
:func:`flux_kvs_commit_get_treeobj`, :func:`flux_kvs_commit_get_rootref()`,
and :func:`flux_kvs_commit_get_sequence` can return information about the
root snapshot that the commit has completed its transaction on.

:func:`flux_kvs_commit_get_treeobj` obtains the root hash in the form of
an RFC 11 *dirref* treeobj, suitable to be passed to
:man3:`flux_kvs_lookupat`.

:func:`flux_kvs_commit_get_rootref` retrieves the blobref for the root.

:func:`flux_kvs_commit_get_sequence` retrieves the monotonic sequence number
for the root.


FLAGS
=====

The following are valid bits in a :var:`flags` mask passed as an argument
to :func:`flux_kvs_commit`.

FLUX_KVS_NO_MERGE
   The KVS service may merge contemporaneous commit transactions as an
   optimization. However, if the combined transactions modify the same key,
   a watch on that key may only be notified of the last-in value. This flag
   can be used to disable that optimization for this transaction.


RETURN VALUE
============

:func:`flux_kvs_commit` returns a :type:`flux_future_t` on success, or NULL
on failure with :var:`errno` set appropriately.


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

.. include:: common/resources.rst


SEE ALSO
========

:man3:`flux_future_get`, :man3:`flux_kvs_txn_create`
