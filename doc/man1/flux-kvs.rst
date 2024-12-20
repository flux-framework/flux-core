===========
flux-kvs(1)
===========


SYNOPSIS
========

| **flux** **kvs** **get** [*--waitcreate*] [*--watch*] [*--raw*] *key...*
| **flux** **kvs** **put** [*--append*] [*--raw*] *key=value...*
| **flux** **kvs** **dir** [*-R*] [*-d*] [*key*]
| **flux** **kvs** **ls** [*-R*] [*-d*] [*-1*] [*-F*]  *key...*
| **flux** **kvs** **unlink** [*-R*] [*-f*] *key...*
| **flux** **kvs** **link** *target* *linkname*
| **flux** **kvs** **readlink** *key...*
| **flux** **kvs** **mkdir** *key...*
| **flux** **kvs** **dropcache**

| **flux** **kvs** **copy** *source* *destination*
| **flux** **kvs** **move** *source* *destination*

| **flux** **kvs** **getroot**
| **flux** **kvs** **version**
| **flux** **kvs** **wait** *version*

| **flux** **kvs** **namespace** **create** [*-o owner*] *name...*
| **flux** **kvs** **namespace** **remove** *name...*
| **flux** **kvs** **namespace** **list**

| **flux** **kvs** **eventlog** **append** *key* *name* [*context...*]
| **flux** **kvs** **eventlog** **get** [*--waitcreate*] [*--watch*] [*-u*] *key*
| **flux** **kvs** **eventlog** **wait-event** [*-v*] [*--waitcreate*] *key* *event*


DESCRIPTION
===========

The Flux key-value store (KVS) is a simple, distributed data storage
service used a building block by other Flux components.
:program:`flux kvs` is a command line utility that operates on the KVS.

The Flux KVS stores values under string keys. The keys are
hierarchical, using "." as a path separator, analogous to "/"
separated UNIX file paths. A single "." represents the root directory
of the KVS.

The KVS is distributed among the broker ranks of a Flux instance. Rank 0
is the leader, and other ranks are caching followers. All writes are flushed
to the leader during a commit operation. Data is stored in a hash tree
such that every commit results in a new root hash. Each new root hash
is multicast across the Flux instance. When followers update their root hash,
they atomically update their view to match the leader. There may be a
delay after a commit while old data is served on a follower that has not yet
updated its root hash, thus the Flux KVS cache is "eventually consistent".
Followers expire cache data after a period of disuse, and fault in new data
through their parent in the overlay network.

The primary KVS namespace is only accessible to the Flux instance owner.
Other namespaces may be created and assigned to guest users.  Although the
cache is shared across namespaces, each has an independent root directory,
which permits commits in multiple namespaces to complete in parallel.


COMMANDS
========

.. program:: flux kvs

The :program:`flux kvs` sub-commands and their arguments are described below.

The following options are common to most sub-commands:

.. option:: -h, --help

  Display help on this sub-command.

.. option:: -N, --namespace=NAME

  Specify an alternate namespace.  By default, the primary KVS namespace
  or the value of :envvar:`FLUX_KVS_NAMESPACE` is used.

get
---

.. program:: flux kvs get

Retrieve the value stored under *key* and print it on standard output.
It is an error if *key* does not exist, unless :option:``--waitcreate`` is
specified.  It is an error if *key* is a directory.

If multiple *key* arguments are specified, their values are concatenated.
A newline is appended to each value, unless :option:`--raw` is specified or
the value is zero length.

.. option:: -r, --raw

  Display value without a newline.

.. option:: -t, --treeobj

  Display RFC 11 tree object.

.. option:: -a, --at=TREEOBJ

  Perform the lookup relative to a directory reference in RFC 11 tree object
  format.

.. option:: -l, --label

  Add *key=* prefix to output.

.. option:: -W, --waitcreate

  If the key does not exist, wait until it does, then return its value.

.. option:: -w, --watch

  Display the initial value for *key*, then watch for changes and display
  each new value until interrupted or :option:`--count=N` is reached.

.. option:: -c, --count=N

  With :option:`--watch`, display *N* values then exit.

.. option:: -u, --uniq

  With :option:`--watch`, suppress output when value is the same, even
  if the watched key was the target of a KVS commit.

.. option:: -A, --append

  With :option:`--watch`, display only the new data when the watched
  value is appended to.

.. option:: -f, --full

  With :option:`--watch`, monitor key changes with more complete accuracy.

  By default, only a direct write to a key is monitored, thus changes that
  occur indirectly could be missed, such as when the parent directory is
  replaced.  The :option:`--full` option ensures these changes are reported
  as well, at greater overhead.

.. option:: -S, --stream

  Return potentially large values in multiple responses.  This may improve
  response times of very large values in the KVS.

  Will not work in combination with :option:`--watch`.

put
---

.. program:: flux kvs put

Set *key* to *value*.  If *key* exists, the current value is overwritten.
If multiple *key=value* pairs are specified, they are sent as one
commit and succeed or fail atomically.

.. option:: -O, --treeobj-root

   After the commit has completed, display the new root directory reference
   in RFC 11 tree object format.

.. option:: -b, --blobref

   After the commit has completed, display the new root directory reference
   as a single blobref.

.. option:: -s, --sequence

   After the commit has completed, display the new root sequence number
   or "version".

.. option:: -r, --raw

   Store *value* as-is, without adding NUL termination.
   If *value* is ``-``, read it from standard input.
   *value* may include embedded NUL bytes.

.. option:: -t, --treeobj

   Interpret *value* as an RFC 11 tree object to be stored directly.
   If *value* is ``-``, read it from standard input.

.. option:: -n, --no-merge

   Set the ``NO_MERGE`` flag on the commit to ensure it is not combined with
   other commits.  The KVS normally combines contemporaneous commits to save
   work, but since commits succeed or fail atomically, this a commit could
   fail due to collateral damage.  This option prevents that from happening.

.. option:: -A, --append

   Append *value* to key's existing value, if any, instead of overwriting it.

.. option:: -S, --sync

   After the commit has completed, flush pending content and checkpoints
   to disk.  Commits normally complete with data in memory, which ensures a
   subsequent :command:`flux kvs get` would receive the updated value, but
   does not ensure it persists across a Flux instance crash.  This can be
   used to ensure critical data has been written to non-volatile storage.

dir
---

.. program:: flux kvs dir

Display all keys and their values under the directory *key*.  Values that
are too long to fit the terminal width are truncated with "..." appended.
This command fails if *key* does not exist or is not a directory.
If *key* is not provided, ``.`` (root of the namespace) is assumed.

.. option:: -R, --recursive

  Recursively display keys under subdirectories.

.. option:: -d, --directory

  List directory entries but not values.

.. option:: -w, --width=N

  Truncate values to fit in *N* columns instead of the terminal width.
  Specify 0 to avoid truncation entirely.

.. option:: -a, --at=TREEOBJ

  Perform the directory lookup relative to a directory reference in
  RFC 11 tree object format.

ls
--

.. program:: flux kvs ls

Display directory referred to by *key*, or "." (root) if unspecified.  This
sub-command is intended to mimic :linux:man1:`ls` behavior in a limited way.

.. option:: -R, --recursive

  List directory recursively.

.. option:: -d, --directory

  List directory instead of contents.

.. option:: -w, --width=N

  Limit output width to *N* columns.  Specify 0 for unlimited output width.

.. option:: -1, --1

  Force one entry per line.

.. option:: -F, --classify

  Append key type indicator to key: ``.`` for directory.  ``@`` for
  symbolic link.

unlink
------

.. program:: flux kvs unlink

Remove key from the KVS.

.. option:: -O, --treeobj-root

   After the commit has completed, display the new root directory reference
   in RFC 11 tree object format.

.. option:: -b, --blobref

   After the commit has completed, display the new root directory reference
   as a single blobref.

.. option:: -s, --sequence

   After the commit has completed, display the new root sequence number
   or "version".

.. option:: -R, --recursive

   Specify recursive removal of a directory.

.. option:: -f, --force

   Ignore nonexistent keys.

link
----

.. program:: flux kvs link

Create a new name for *target*, similar to a symbolic link.  *target* does not
have to exist.  If *linkname* exists, it is overwritten.

.. option:: -T, --target-namespace=NAME

  Specify an alternate namespace for *target*.  By default, *target* is
  in the same namespace as *linkname*.

.. option:: -O, --treeobj-root

   After the commit has completed, display the new root directory reference
   in RFC 11 tree object format.

.. option:: -b, --blobref

   After the commit has completed, display the new root directory reference
   as a single blobref.

.. option:: -s, --sequence

   After the commit has completed, display the new root sequence number
   or "version".

readlink
---------

.. program:: flux kvs readlink

Print the symbolic link target of *key*.  The target may be another key name,
or a :option:`namespace::key` tuple.  It is an error if *key* is not a
symbolic link.

.. option:: -a, --at=TREEOBJ

  Perform the lookup relative to a directory reference in RFC 11 tree object
  format.

.. option:: -o, --namespace-only

  Print only the namespace name if the target has a namespace prefix.

.. option:: -k, --key-only

  Print only the key if the target has a namespace prefix.

mkdir
-----

.. program:: flux kvs mkdir

Create an empty directory. If *key* exists, it is overwritten.

.. option:: -O, --treeobj-root

   After the commit has completed, display the new root directory reference
   in RFC 11 tree object format.

.. option:: -b, --blobref

   After the commit has completed, display the new root directory reference
   as a single blobref.

.. option:: -s, --sequence

   After the commit has completed, display the new root sequence number
   or "version".

dropcache
---------

.. program:: flux kvs dropcache

Tell the local KVS to drop any cache it is holding.

.. option:: -a, --all

  Publish an event across the Flux instance instructing the KVS module on
  all ranks to drop their caches.

copy
----

.. program:: flux kvs copy

Copy *source* key to *destination* key.  If a directory is copied, a new
reference is created.

.. option:: -S, --src-namespace=NAME

  Specify the source namespace.  By default, the primary KVS namespace
  or the value of :envvar:`FLUX_KVS_NAMESPACE` is used.

.. option:: -D, --dst-namespace=NAME

  Specify the destination namespace.  By default, the primary KVS namespace
  or the value of :envvar:`FLUX_KVS_NAMESPACE` is used.

move
----

.. program:: flux kvs move

Copy *source* key to *destination* key and unlink *source*.

.. option:: -S, --src-namespace=NAME

  Specify the source namespace.  By default, the primary KVS namespace
  or the value of :envvar:`FLUX_KVS_NAMESPACE` is used.

.. option:: -D, --dst-namespace=NAME

  Specify the destination namespace.  By default, the primary KVS namespace
  or the value of :envvar:`FLUX_KVS_NAMESPACE` is used.

getroot
-------

.. program:: flux kvs getroot

Retrieve the current KVS root directory reference, displaying it as an
RFC 11 dirref object unless otherwise specified.

.. option:: -o, --owner

   Display the numerical user ID that owns the target namespace.

.. option:: -b, --blobref

   Display the root directory reference as a single blobref.

.. option:: -s, --sequence

   Display the root sequence number or "version".

version
-------

.. program:: flux kvs version

Display the current KVS version, an integer value. The version starts
at zero and is incremented on each KVS commit. Note that some commits
may be aggregated for performance and the version will be incremented
once for the aggregation, so it cannot be used as a direct count of
commit requests.

wait
----

.. program:: flux kvs wait

Block until the KVS version reaches *version* or greater. A simple form
of synchronization between peers is: node A puts a value, commits it,
reads version, sends version to node B. Node B waits for version, gets value.

namespace create
----------------

.. program:: flux kvs namespace create

Create a new KVS namespace.

.. option:: -o, --owner=UID

  Set the owner of the namespace to *UID*.  If unspecified, the owner is
  set to the Flux instance owner.

.. option:: -r, --rootref=TREEOBJ

  Initialize namespace with specific root directory reference
  If unspecified, an empty directory is referenced.

namespace remove
----------------

.. program:: flux kvs namespace remove

Remove a KVS namespace.  Removal is not guaranteed to be complete when
the command returns.

namespace list
--------------

.. program:: flux kvs namespace list

List all current namespaces, with owner and flags.

eventlog get
------------

.. program:: flux kvs eventlog get

Display the contents of an RFC 18 KVS eventlog referred to by *key*.

.. option:: -W, --waitcreate

  If the key does not exist, wait until it does.

.. option:: -w, --watch

  Monitor the eventlog, displaying new events as they are appended.

.. option:: -c, --count=N

  With :option:`--watch`, exit once *N* events have been displayed.

.. option:: -u, --unformatted

  Display the eventlog in raw RFC 18 form.

.. option:: -H, --human

  Display the eventlog in human-readable form.

.. option:: -L, --color[=WHEN]

  Control output colorization. The optional argument *WHEN* can be one of
  'auto', 'never', or 'always'. The default value of *WHEN* if omitted is
  'always'. The default is 'auto' if the option is unused.

.. option:: -S, --stream

  Return potentially large eventlogs in multiple responses.  This may improve
  response times of very large eventlogs in the KVS.

  Will not work in combination with :option:`--watch`.

eventlog append
---------------

.. program:: flux kvs eventlog append

Append an event to an RFC 18 KVS eventlog referred to by *key*.
The event *name* and optional *context* are specified on the command line.

.. option:: -t, --timestamp=SEC

  Specify timestamp in decimal seconds since the UNIX epoch (UTC), otherwise
  the current wall clock is used.


eventlog wait-event
-------------------

.. program:: flux kvs eventlog wait-event

Wait for a specific *event* to occur in an RFC 18 KVS eventlog
referred to by *key*.

.. option:: -W, --waitcreate

  If the key does not exist, wait until it does.

.. option:: -t, --timeout=SEC

   Timeout after *SEC* if the specified event has not occurred by then.

.. option:: -u, --unformatted

  Display the event(s) in raw RFC 18 form.

.. option:: -q, --quiet

  Do not display the matched event.

.. option:: -v, --verbose

  Display events prior to the matched event.

.. option:: -H, --human

  Display eventlog events in human-readable form.

.. option:: -L, --color[=WHEN]

  Control output colorization. The optional argument *WHEN* can be one of
  'auto', 'never', or 'always'. The default value of *WHEN* if omitted is
  'always'. The default is 'auto' if the option is unused.



RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_11`

:doc:`rfc:spec_18`
