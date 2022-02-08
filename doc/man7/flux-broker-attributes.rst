=========================
flux-broker-attributes(7)
=========================


DESCRIPTION
===========

Flux broker attributes are parameters that affect how different
broker systems behave. Attributes can be listed and manipulated
with :man1:`flux-getattr`, :man1:`flux-setattr`, and
:man1:`flux-lsattr`.

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
   path limit described in :linux:man7:`unix`, as low as 92 bytes on
   some systems.  If rundir is set to a pre-existing directory, the
   directory is not removed on exit; if the broker has to create the
   directory, it is removed.  In most cases this attribute should not
   be set by users.

content.backing-path
   The path to the content backing store file(s). If this is set on the
   broker command line, the backing store uses this path instead of
   a temporary one, and content is preserved on instance exit.
   If file exists, its content is imported into the instance.
   If it doesn't exist, it is created.

hostlist
   An RFC 29 hostlist in broker rank order.

broker.starttime
   Timestamp of broker startup from :man3:`flux_reactor_now`.


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

tbon.parent-endpoint
   The ZeroMQ endpoint of this broker's TBON parent.

tbon.zmqdebug
   If set to an non-zero integer value, 0MQ socket event logging is enabled,
   if available.  This is potentially useful for debugging overlay
   connectivity problems.  The attribute may not be changed during runtime.

tbon.prefertcp
   If set to an integer value other than zero, and the broker is bootstrapping
   with PMI, tcp:// endpoints will be used instead of ipc://, even if all
   brokers are on a single node.

tbon.torpid_min
   The amount of time (in RFC 23 Flux Standard Duration format) that a broker
   will allow the connection to its TBON parent to remain idle before sending a
   control message to indicate create activity.  This value may be adjusted
   on a live system.

tbon.torpid_max
   The amount of time (in RFC 23 Flux Standard Duration format) that a broker
   will wait for an idle TBON child connection to send messages before
   declaring it torpid (unresponsive).  A value of 0 disables torpid node
   checking.  Torpid nodes are automatically drained and require manual
   undraining with :man1:`flux-resource`.  This value may be adjusted on a
   live system.

tbon.keepalive_enable
   An integer value to disable (0) or enable (1) TCP keepalives on TBON
   child connections.  TCP keepalives are required to detect abruptly turned
   off peers that are unable to shutdown their TCP connection.  Default 1
   or as configured in :man5:`flux-config-tbon`.

tbon.keepalive_count
   The integer number of TCP keepalive packets to send to an idle downstream
   peer with no response before disconnecting it.  Set to -1 to use the
   system value from :linux:man8:`sysctl` ``net.ipv4.tcp_keepalive_probes``.
   Default -1 or as configured in :man5:`flux-config-tbon`.

tbon.keepalive_idle
   The integer number of seconds to wait for an idle downstream peer to send
   messages before beginning to send keepalive packets.  Set to -1 to use the
   system value from :linux:man8:`sysctl` ``net.ipv4.tcp_keepalive_time``.
   Default -1 or as configured in :man5:`flux-config-tbon`.

tbon.keepalive_interval
   The integer number of seconds to wait between sending keepalive packets.
   Set to -1 to use the system value from :linux:man8:`sysctl`
   ``net.ipv4.tcp_keepalive_intvl``.  Default -1 or as configured in
   :man5:`flux-config-tbon`.


SOCKET ATTRIBUTES
=================

local-uri
   The Flux URI that should be passed to :man3:`flux_open` to
   establish a connection to the local broker rank. By default,
   local-uri is created as "local://<broker.rank>/local".

parent-uri
   The Flux URI that should be passed to :man3:`flux_open` to
   establish a connection to the enclosing instance.


LOGGING ATTRIBUTES
==================

log-ring-used
   The number of log entries currently stored in the ring buffer.

log-ring-size
   The maximum number of log entries that can be stored in the ring buffer.

log-count
   The number of log entries ever stored in the ring buffer.

log-forward-level
   Log entries at :linux:man3:`syslog` level at or below this value
   are forwarded to rank zero for permanent capture.

log-critical-level
   Log entries at :linux:man3:`syslog` level at or below this value
   are copied to stderr on the logging rank, for capture by the
   enclosing instance.

log-filename
   (rank zero only) If set, session log entries, as filtered by log-forward-level,
   are directed to this file.

log-stderr-mode
   If set to "leader" (default), broker rank 0 emits forwarded logs from
   other ranks to stderr, subject to the constraints of log-forward-level
   and log-stderr-level.  If set to "local", each broker emits its own
   logs to stderr, subject to the constraints of log-stderr-level.

log-stderr-level
   Log entries at :linux:man3:`syslog` level at or below this value to
   stderr, subject to log-stderr-mode.

log-level
   Log entries at :linux:man3:`syslog` level at or below this value
   are stored in the ring buffer.


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


RESOURCES
=========

Flux: http://flux-framework.org

RFC 23: Flux Standard Duration: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_23.html

RFC 29: Hostlist Format: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_29.html


SEE ALSO
========

:man1:`flux-getattr`, :man3:`flux_attr_get`
