=========================
flux-broker-attributes(7)
=========================


DESCRIPTION
===========

Flux broker attributes are parameters that affect how different
broker systems behave. Attributes can be listed and manipulated
with flux-getattr(1), flux-setattr(1), and flux-lsattr(1).

The broker currently exports the following attributes:


SESSION ATTRIBUTES
==================

rank
   The rank of the local broker.

size
   The number of broker ranks in the flux instance

rundir
   A temporary directory where UNIX domain sockets and the default
   content.backing-path are located (see below).  By default, each broker
   rank creates a unique rundir in $TMPDIR and removes it on exit.  If
   rundir is set on the command line, beware exceeding the UNIX domain socket
   path limit described in unix(7), as low as 92 bytes on some systems.
   If rundir is set to a pre-existing directory, the directory is not removed
   on exit;  if the broker has to create the directory, it is removed.
   In most cases this attribute should not be set by users.

content.backing-path
   The path to the content backing store file(s). If this is set on the
   broker command line, the backing store uses this path instead of
   a temporary one, and content is preserved on instance exit.
   If file exists, its content is imported into the instance.
   If it doesn't exist, it is created.


TOPOLOGY ATTRIBUTES
===================

tbon.fanout
   Branching factor of the tree based overlay network.

tbon.descendants
   Number of descendants "below" this node of the tree based
   overlay network, not including this node.

tbon.level
   The level of this node in the tree based overlay network.
   Root is level 0.

tbon.maxlevel
   The maximum level number in the tree based overlay network.
   Maxlevel is 0 for a size=1 instance.

tbon.endpoint
   The endpoint for the tree based overlay network to communicate over.

tbon.zmqdebug
   If set to an non-zero integer value, 0MQ socket event logging is enabled,
   if available.  This is potentially useful for debugging overlay
   connectivity problems.  The attribute may not be changed during runtime.

tbon.prefertcp
   If set to an integer value other than zero, and the broker is bootstrapping
   with PMI, tcp:// endpoints will be used instead of ipc://, even if all
   brokers are on a single node.


SOCKET ATTRIBUTES
=================

tbon.parent-endpoint
   The URI of the ZeroMQ endpoint this rank is connected to in the tree
   based overlay network. This attribute will not be set on rank zero.

local-uri
   The Flux URI that should be passed to flux_open(1) to establish
   a connection to the local broker rank. By default, local-uri is
   created as "local://<broker.rank>/local".

parent-uri
   The Flux URI that should be passed to flux_open(1) to establish
   a connection to the enclosing instance.


LOGGING ATTRIBUTES
==================

log-ring-used
   The number of log entries currently stored in the ring buffer.

log-ring-size
   The maximum number of log entries that can be stored in the ring buffer.

log-count
   The number of log entries ever stored in the ring buffer.

log-forward-level
   Log entries at syslog(3) level at or below this value are forwarded
   to rank zero for permanent capture.

log-critical-level
   Log entries at syslog(3) level at or below this value are copied
   to stderr on the logging rank, for capture by the enclosing instance.

log-filename
   (rank zero only) If set, session log entries, as filtered by log-forward-level,
   are directed to this file.

log-stderr-mode
   If set to "leader" (default), broker rank 0 emits forwarded logs from
   other ranks to stderr, subject to the constraints of log-forward-level
   and log-stderr-level.  If set to "local", each broker emits its own
   logs to stderr, subject to the constraints of log-stderr-level.

log-stderr-level
   Log entries at syslog(3) level at or below this value to stderr,
   subject to log-stderr-mode.

log-level
   Log entries at syslog(3) level at or below this value are stored
   in the ring buffer.


CONTENT ATTRIBUTES
==================

content.acct-dirty
   The number of dirty cache entries on this rank.

content.acct-entries
   The total number of cache entries on this rank.

content.acct-size
   The estimated total size in bytes consumed by cache entries on
   this rank, excluding overhead.

content.acct-valid
   The number of valid cache entries on this rank.

content.backing-module
   The selected backing store module, if any. This attribute is only
   set on rank 0 where the content backing store is active.

content.blob-size-limit
   The maximum size of a blob, the basic unit of content storage.

content.flush-batch-count
   The current number of outstanding store requests, either to the
   backing store (rank 0) or upstream (rank > 0).

content.flush-batch-limit
   The maximum number of outstanding store requests that will be
   initiated when handling a flush or backing store load operation.

content.hash
   The selected hash algorithm, default sha1.

content.purge-old-entry
   When the cache size footprint needs to be reduced, only consider
   purging entries that are older than this number of seconds.

content.purge-target-size
   If possible, the cache size purged periodically so that the total
   size of the cache stays at or below this value.


WIREUP ATTRIBUTES
=================

hello.timeout
   The reduction timeout (in seconds) for the broker wireup protocol.
   Before the timeout, a topology-based high water mark is applied
   at each node of the tree based overlay network. After the timeout,
   new wireup information is forwarded upstream without delay.
   Set to 0 to disable the timeout.

hello.hwm
   The reduction high water mark for the broker wireup protocol,
   normally calculated based on the topology.
   Set to 0 to disable the high water mark.


CONFIG ATTRIBUTES
=================

broker.hostlist
   The rank-ordered hosts specified in the ``bootstrap`` section of
   the Flux configuration.  Hosts are listed in RFC29 hostlist format.


RESOURCES
=========

Github: http://github.com/flux-framework


SEE ALSO
========

flux-getattr(1), flux_attr_get(3)
