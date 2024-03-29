===================
flux_kvs_getroot(3)
===================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   flux_future_t *flux_kvs_getroot (flux_t *h,
                                    const char *ns,
                                    int flags);

   int flux_kvs_getroot_get_treeobj (flux_future_t *f,
                                     const char **treeobj);

   int flux_kvs_getroot_get_blobref (flux_future_t *f,
                                     const char **blobref);

   int flux_kvs_getroot_get_sequence (flux_future_t *f, int *seq);

   int flux_kvs_getroot_get_owner (flux_future_t *f, uint32_t *owner);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_kvs_getroot` sends a request via handle :var:`h` to the ``kvs``
service to look up the current root hash for namespace :var:`ns`. A
:type:`flux_future_t` object is returned, which acts as handle for
synchronization and container for the response. :var:`flags` is currently
unused and should be set to 0.

Upon future fulfillment, these functions can decode the result:

:func:`flux_kvs_getroot_get_treeobj` obtains the root hash in the form
of an RFC 11 *dirref* treeobj, suitable to be passed to :man3:`flux_kvs_lookupat`.

:func:`flux_kvs_getroot_get_blobref` obtains the RFC 10 blobref, suitable to
be passed to :man3:`flux_content_load`.

:func:`flux_kvs_getroot_get_sequence` retrieves the monotonic sequence number
for the root.

:func:`flux_kvs_getroot_get_owner` retrieves the namespace owner.


FLAGS
=====

The :var:`flags` mask is currently unused and should be set to 0.


RETURN VALUE
============

:func:`flux_kvs_getroot` returns a :type:`flux_future_t` on success, or NULL
on failure with :var:`errno` set appropriately.

The other functions return zero on success, or -1 on failure with :var:`errno`
set appropriately.


ERRORS
======

EINVAL
   One of the arguments was invalid.

ENOMEM
   Out of memory.

EPROTO
   A request was malformed.

ENOSYS
   The kvs module is not loaded.

ENOTSUP
   An unknown namespace was requested or namespace was deleted.

EPERM
   The requesting user is not permitted to access the requested namespace.

ENODATA
   A stream of responses has been terminated by a call to
   :func:`flux_kvs_getroot_cancel`.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man3:`flux_kvs_lookup`, :man3:`flux_future_get`, :man3:`flux_content_load`
