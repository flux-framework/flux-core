===============
flux-sqlite(1)
===============


SYNOPSIS
========

| **flux** **sqlite** **query** [*OPTIONS*] *query*
| **flux** **sqlite** **backup** *path*


DESCRIPTION
===========

:program:`flux sqlite` provides administrative access to the content-sqlite
database via RPC. This allows instance owners to inspect database contents,
perform maintenance operations, and create backups without direct database
file access.

All operations require instance owner credentials and are only available
when the content-sqlite backing store module is loaded.

The content-sqlite database stores content blobs on the leader broker (rank 0)
and is primarily used by the Flux KVS.


COMMANDS
========

query
-----

.. program:: flux sqlite query

Execute a SQL query against the content-sqlite database.

Supported SQL operations:

**SELECT**
   Query database contents. Useful for inspecting stored objects,
   checking database size, or debugging KVS issues.

**PRAGMA**
   View or modify database settings such as journal_mode, synchronous mode,
   page_count, etc.

**DELETE**
   Remove specific rows from the database. Requires :option:`--force` flag.
   Use with caution as deleted blobs cannot be recovered.

**VACUUM**
   Reclaim space from deleted rows by rebuilding the database file.
   Requires :option:`--force` flag. This operation can be slow on large
   databases.

Other SQL operations (INSERT, UPDATE, CREATE, DROP, etc.) are not allowed
as they could corrupt the database.

.. option:: -H, --header

   Print column headers in output.

.. option:: -c, --column

   Use column mode output with aligned columns.

.. option:: -p, --param PARAM

   Add a parameter to the query. This option can be specified multiple
   times for queries with multiple placeholders (``?``). Parameters are
   automatically typed:

   * Integers and floats are parsed to their respective types
   * The literal string ``null`` becomes SQL NULL
   * ``blob:HEXSTRING`` encodes a hex string as a BLOB (e.g.,
     ``blob:a1b2c3``)
   * All other values are treated as TEXT

   Parameters enable safe, SQL-injection-proof queries when working with
   dynamic values.

.. option:: --force

   Allow destructive operations (DELETE, VACUUM). This flag prevents
   accidental data loss by requiring explicit confirmation for operations
   that modify or optimize the database.

backup
------

.. program:: flux sqlite backup

Create an online backup of the content-sqlite database using SQLite's
backup API. The backup is created atomically and consistently, even while
the database is being actively used.

The backup *path* must be an absolute path and cannot be the same as the
source database file.

This operation does not require the :option:`--force` flag as it is
non-destructive.


EXAMPLES
========

Query the number of objects in the database::

  $ flux sqlite query "SELECT COUNT(*) FROM objects"
  42

View database settings::

  $ flux sqlite query "PRAGMA journal_mode"
  wal

Inspect the schema::

  $ flux sqlite query -H -c "PRAGMA table_info(objects)"

Check database statistics::

  $ flux sqlite query "PRAGMA page_count"
  $ flux sqlite query "PRAGMA page_size"

Query total database size::

  $ flux sqlite query "SELECT COUNT(*), SUM(size) FROM objects"

Query with a parameterized integer value::

  $ flux sqlite query -p 100 "SELECT COUNT(*) FROM objects WHERE size > ?"

Query with multiple parameters::

  $ flux sqlite query -p 10 -p 1000 \
    "SELECT * FROM objects WHERE size > ? AND size < ?"

Delete a specific object by hash using a BLOB parameter::

  $ flux sqlite query --force -p blob:a1b2c3d4e5 \
    "DELETE FROM objects WHERE hash = ?"

Reclaim space after garbage collection::

  $ flux sqlite query --force "VACUUM"

Create a backup::

  $ flux sqlite backup /tmp/content-backup.db


CAVEATS
=======

**Destructive Operations**

DELETE and VACUUM operations permanently modify the database. Deleted
content blobs cannot be recovered. Always verify queries carefully and
consider creating a backup before destructive operations.

RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man1:`flux-content`, :man1:`flux-kvs`, :man1:`flux-dump`
