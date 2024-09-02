=============================
flux-config-content-sqlite(5)
=============================


DESCRIPTION
===========

The Flux system may configure one of several possible content backends for
storing data to long term disk.  The ``content-sqlite`` module is one
such backend, using ``sqlite`` for storage.

The ``content-sqlite`` table may contain the following keys:


KEYS
====

journal_mode
   (optional) Configure the sqlite journal mode.  By default is set to ``WAL``
   if a flux statedir is configured, otherwise defaults to ``OFF``.  Legal options
   are ``DELETE``, ``TRUNCATE``, ``PERSIST``, ``MEMORY``, ``WAL``, and ``OFF``.

synchronous
   (optional) Configure the sqlite synchronous mode.  By default is set to ``NORMAL``
   if a flux statedir is configured, otherwise defaults to ``OFF``.  Legal options
   are ``EXTRA``, ``FULL``, ``NORMAL``, ``OFF``.

preallocate
   (optional) Specify a number of bytes to preallocate to the sqlite database.  This
   can ensure the module can continue to function even if the disk has been full
   due to other operations on the node (e.g. system logging has filled up disk).  Using
   preallocated space requires journaling to be disabled, which will automatically be
   done if the disk is out of space and the preallocate setting is configured.


EXAMPLES
========

::

   [content-sqlite]
   journal_mode = "WAL"
   synchronous = "NORMAL"
   preallocate = 10737418240


SEE ALSO
========

:man5:`flux-config`,
`sqlite <https://www.sqlite.org/>`_,
`sqlite pragmas <https://www.sqlite.org/pragma.html>`_
