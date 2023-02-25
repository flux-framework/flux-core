======================
flux_kvs_txn_create(3)
======================


SYNOPSIS
========

::

   #include <flux/core.h>

::

   flux_kvs_txn_t *flux_kvs_txn_create (void);

::

   void flux_kvs_txn_destroy (flux_kvs_txn_t *txn);

::

   int flux_kvs_txn_put (flux_kvs_txn_t *txn, int flags,
                         const char *key, const char *value);

::

   int flux_kvs_txn_pack (flux_kvs_txn_t *txn, int flags,
                          const char *key, const char *fmt, ...);

::

   int flux_kvs_txn_vpack (flux_kvs_txn_t *txn, int flags,
                           const char *key, const char *fmt, va_list ap);

::

   int flux_kvs_txn_mkdir (flux_kvs_txn_t *txn, int flags,
                           const char *key);

::

   int flux_kvs_txn_unlink (flux_kvs_txn_t *txn, int flags,
                            const char *key);

::

   int flux_kvs_txn_symlink (flux_kvs_txn_t *txn, int flags,
                             const char *key, const char *ns,
                             const char *target);

::

   int flux_kvs_txn_put_raw (flux_kvs_txn_t *txn, int flags,
                             const char *key, const void *data, int len);

::

   int flux_kvs_txn_put_treeobj (flux_kvs_txn_t *txn, int flags,
                                 const char *key, const char *treeobj);


DESCRIPTION
===========

The Flux Key Value Store is a general purpose distributed storage
service used by Flux services.

``flux_kvs_txn_create()`` creates a KVS transaction object that may be
passed to :man3:`flux_kvs_commit` or :man3:`flux_kvs_fence`. The transaction
consists of a list of operations that are applied to the KVS together,
in order. The entire transaction either succeeds or fails. After commit
or fence, the object must be destroyed with ``flux_kvs_txn_destroy()``.

Each function below adds a single operation to *txn*. *key* is a
hierarchical path name with period (".") used as path separator.
When the transaction is committed, any existing keys or path components
that are in conflict with the requested operation are overwritten.
*flags* can modify the request as described below.

``flux_kvs_txn_put()`` sets *key* to a NULL terminated string *value*.
*value* may be NULL indicating that an empty value should be stored.

``flux_kvs_txn_pack()`` sets *key* to a NULL terminated string encoded
from a JSON object built with ``json_pack()`` style arguments (see below).
``flux_kvs_txn_vpack()`` is a variant that accepts a *va_list* argument.

``flux_kvs_txn_mkdir()`` sets *key* to an empty directory.

``flux_kvs_txn_unlink()`` removes *key*. If *key* is a directory,
all its contents are removed as well.

``flux_kvs_txn_symlink()`` sets *key* to a symbolic link pointing to a
namespace *ns* and a *target* key within that namespace. Neither *ns*
nor *target* must exist. The namespace *ns* is optional, if set to
NULL the *target* is assumed to be in the key's current namespace.

``flux_kvs_txn_put_raw()`` sets *key* to a value containing raw data
referred to by *data* of length *len*.

``flux_kvs_txn_put_treeobj()`` sets *key* to an RFC 11 object, encoded
as a JSON string.


FLAGS
=====

The following are valid bits in a *flags* mask passed as an argument
to ``flux_kvs_txn_put()`` or ``flux_kvs_txn_put_raw()``.

FLUX_KVS_APPEND
   Append value instead of overwriting it. If the key does not exist,
   it will be created with the value as the initial value.

.. include:: common/json_pack.rst


RETURN VALUE
============

``flux_kvs_txn_create()`` returns a ``flux_kvs_txn_t`` object on success,
or NULL on failure with errno set appropriately.

``flux_kvs_txn_put()``, ``flux_kvs_txn_pack()``, ``flux_kvs_txn_mkdir()``,
``flux_kvs_txn_unlink()``, ``flux_kvs_txn_symlink()``, and ``flux_kvs_txn_put_raw()``
returns 0 on success, or -1 on failure with errno set appropriately.


ERRORS
======

EINVAL
   One of the arguments was invalid.

ENOMEM
   Out of memory.


RESOURCES
=========

Flux: http://flux-framework.org

RFC 11: Key Value Store Tree Object Format v1: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_11.html


SEE ALSO
========

:man3:`flux_kvs_commit`
