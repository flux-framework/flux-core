======================
flux_kvs_txn_create(3)
======================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   flux_kvs_txn_t *flux_kvs_txn_create (void);

   void flux_kvs_txn_destroy (flux_kvs_txn_t *txn);

   int flux_kvs_txn_put (flux_kvs_txn_t *txn,
                         int flags,
                         const char *key,
                         const char *value);

   int flux_kvs_txn_pack (flux_kvs_txn_t *txn,
                          int flags,
                          const char *key,
                          const char *fmt,
                          ...);

   int flux_kvs_txn_vpack (flux_kvs_txn_t *txn,
                           int flags,
                           const char *key,
                           const char *fmt,
                           va_list ap);

   int flux_kvs_txn_mkdir (flux_kvs_txn_t *txn,
                           int flags,
                           const char *key);

   int flux_kvs_txn_unlink (flux_kvs_txn_t *txn,
                            int flags,
                            const char *key);

   int flux_kvs_txn_symlink (flux_kvs_txn_t *txn,
                             int flags,
                             const char *key,
                             const char *ns,
                             const char *target);

   int flux_kvs_txn_put_raw (flux_kvs_txn_t *txn,
                             int flags,
                             const char *key,
                             const void *data,
                             size_t len);

   int flux_kvs_txn_put_treeobj (flux_kvs_txn_t *txn,
                                 int flags,
                                 const char *key,
                                 const char *treeobj);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

The Flux Key Value Store is a general purpose distributed storage
service used by Flux services.

:func:`flux_kvs_txn_create` creates a KVS transaction object that may be
passed to :man3:`flux_kvs_commit` or :man3:`flux_kvs_fence`. The transaction
consists of a list of operations that are applied to the KVS together,
in order. The entire transaction either succeeds or fails. After commit
or fence, the object must be destroyed with :func:`flux_kvs_txn_destroy`.

Each function below adds a single operation to :var:`txn`. :var:`key` is a
hierarchical path name with period (".") used as path separator.
When the transaction is committed, any existing keys or path components
that are in conflict with the requested operation are overwritten.
:var:`flags` can modify the request as described below.

:func:`flux_kvs_txn_put` sets :var:`key` to a NULL terminated string
:var:`value`.  :var:`value` may be NULL indicating that an empty value should
be stored.

:func:`flux_kvs_txn_pack` sets :var:`key` to a NULL terminated string encoded
from a JSON object built with :func:`json_pack` style arguments (see below).
:func:`flux_kvs_txn_vpack` is a variant that accepts a :type:`va_list` argument.

:func:`flux_kvs_txn_mkdir` sets :var:`key` to an empty directory.

:func:`flux_kvs_txn_unlink` removes :var:`key`. If :var:`key` is a directory,
all its contents are removed as well.

:func:`flux_kvs_txn_symlink` sets :var:`key` to a symbolic link pointing to a
namespace :var:`ns` and a :var:`target` key within that namespace. Neither
:var:`ns` nor :var:`target` must exist. The namespace :var:`ns` is optional,
if set to NULL the :var:`target` is assumed to be in the key's current
namespace.

:func:`flux_kvs_txn_put_raw` sets :var:`key` to a value containing raw data
referred to by :var:`data` of length :var:`len`.

:func:`flux_kvs_txn_put_treeobj` sets :var:`key` to an RFC 11 object, encoded
as a JSON string.


FLAGS
=====

The following are valid bits in a :var:`flags` mask passed as an argument
to :func:`flux_kvs_txn_put` or :func:`flux_kvs_txn_put_raw`.

FLUX_KVS_APPEND
   Append value instead of overwriting it. If the key does not exist,
   it will be created with the value as the initial value.


ENCODING JSON PAYLOADS
======================

.. include:: common/json_pack.rst


RETURN VALUE
============

:func:`flux_kvs_txn_create` returns a :type:`flux_kvs_txn_t` object on success,
or NULL on failure with :var:`errno` set appropriately.

:func:`flux_kvs_txn_put`, :func:`flux_kvs_txn_pack`, :func:`flux_kvs_txn_mkdir`,
:func:`flux_kvs_txn_unlink`, :func:`flux_kvs_txn_symlink`, and
:func:`flux_kvs_txn_put_raw` returns 0 on success, or -1 on failure with
:var:`errno` set appropriately.


ERRORS
======

EINVAL
   One of the arguments was invalid.

ENOMEM
   Out of memory.


RESOURCES
=========

.. include:: common/resources.rst

FLUX RFC
========

:doc:`rfc:spec_11`


SEE ALSO
========

:man3:`flux_kvs_commit`
