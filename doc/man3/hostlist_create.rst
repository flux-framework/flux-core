==================
hostlist_create(3)
==================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

  #include <flux/hostlist.h>

  struct hostlist *hostlist_create (void);

  void hostlist_destroy (struct hostlist *hl);

  struct hostlist *hostlist_decode (const char *s);

  char *hostlist_encode (struct hostlist *hl);

  struct hostlist *hostlist_copy (const struct hostlist *hl);

  int hostlist_count (struct hostlist *hl);

  int hostlist_append (struct hostlist *hl, const char *hosts);

  int hostlist_append_list (struct hostlist *hl1, struct hostlist *hl2);

  const char *hostlist_nth (struct hostlist *hl, int n);

  int hostlist_find (struct hostlist *hl, const char *hostname);

  int hostlist_delete (struct hostlist *hl, const char *hosts);

  void hostlist_sort (struct hostlist *hl);

  void hostlist_uniq (struct hostlist *hl);

  const char *hostlist_first (struct hostlist *hl);

  const char *hostlist_last (struct hostlist *hl);

  const char *hostlist_next (struct hostlist *hl);

  const char *hostlist_current (struct hostlist *hl);

  int hostlist_remove_current (struct hostlist *hl);

Link with :command:`-lflux-hostlist`.

DESCRIPTION
===========

A hostlist is an ordered list of hostnames that can be encoded as a
compact string when the hostnames contain numerical indices.  For example,
the hostnames ``test0``, ``test1``, ... ``test127`` may be encoded to
``test[0-127]``.  Hostlists are further described in
:doc:`Flux RFC 29 <rfc:spec_29>`.

The hostlist contains an internal cursor that is used for iteration.
For the functions below that return a :type:`const char *` hostname,
the returned value may be assumed to remain valid only until the next
call to a function that updates the cursor.

:func:`hostlist_create` creates an empty hostlist.

:func:`hostlist_destroy` destroys a hostlist.

:func:`hostlist_decode` converts an RFC 29 hostlist string into a hostlist.
The caller must free the result with :func:`hostlist_destroy`.

:func:`hostlist_encode` converts a hostlist into an RFC 29 hostlist string.
The caller must free the result with :linux:man3:`free`.

:func:`hostlist_copy` makes a copy of a hostlist.
The caller must free the result with :func:`hostlist_destroy`.

:func:`hostlist_count` returns the number of hostnames in a hostlist.

:func:`hostlist_append` decodes an RFC 29 hostlist string and appends its
hostnames to another hostlist.

:func:`hostlist_append_list` appends the hostnames of a hostlist to another
hostlist.

:func:`hostlist_nth` sets the cursor to the hostname at index :var:`n`
(zero origin) and returns it.

:func:`hostlist_find` sets the cursor to the position of :var:`hostname`
and returns its zero-origin index.

:func:`hostlist_delete` deletes an RFC 29 hostlist string from a hostlist.
If the cursor hostname is deleted, the cursor is advanced to the next valid
hostname.

:func:`hostlist_sort` sorts a hostlist object.  The cursor may be updated.

:func:`hostlist_uniq` sorts a hostlist object, then removes duplicate
hostnames.  The cursor may be updated.

:func:`hostlist_first` sets the cursor to the first hostname and returns it.

:func:`hostlist_last` sets the cursor to the last hostname and returns it.

:func:`hostlist_next` sets the cursor to next hostname and returns it.

:func:`hostlist_current` returns the hostname at the cursor.

:func:`hostlist_remove_current` removes the hostname at the cursor and sets
the cursor to the next hostname.

RETURN VALUE
============

:func:`hostlist_create`, :func:`hostlist_decode`, and :func:`hostlist_copy`
return a hostlist on success which must be freed with :func:`hostlist_destroy`.
On failure, NULL is returned with :var:`errno` set.

:func:`hostlist_encode` returns a string on success that must be freed.
On failure, NULL is returned with :var:`errno` set.

:func:`hostlist_append`, :func:`hostlist_append_list`, :func:`hostlist_delete`,
and :func:`hostlist_remove_current` return a count on success.
On failure, -1 is returned with :var:`errno` set.

:func:`hostlist_count` returns a count.  If the hostlist is invalid, zero
is returned.

:func:`hostlist_find` returns an index on success.
On failure, -1 is returned with :var:`errno` set.

:func:`hostlist_sort` and :func:`hostlist_uniq` return nothing.

Other functions return a string on success, or NULL on failure with
:var:`errno` set.

ERRORS
======

EINVAL
  One or more arguments were invalid.

ENOMEM
  Out of memory.

ERANGE
  Internal maximum numerical range span was exceeded.

ENOENT
  Index or hostname was not found.

RESOURCES
=========

.. include:: common/resources.rst

FLUX RFC
========

:doc:`rfc:spec_29`
