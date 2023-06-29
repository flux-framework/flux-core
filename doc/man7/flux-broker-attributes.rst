=========================
flux-broker-attributes(7)
=========================


DESCRIPTION
===========

Flux broker attributes are broker parameters with a scope of a single broker
rank.  They may be listed with :man1:`flux-lsattr` and queried with
:man1:`flux-getattr`.

Attributes should be considered read-only, unless annotated below with:

C
   The attribute may be set on the :man1:`flux-broker` command line with
   ``--setattr=NAME=VALUE``.

R
   The attribute may be updated on a running broker with :man1:`flux-setattr`.


GENERAL
=======

rank
   The rank of the local broker.

size
   The number of broker ranks in the flux instance

version
   The version of flux-core that was used to build this broker.

rundir [Updates: C]
   A temporary directory where the broker's UNIX domain sockets are located.
   In addition, if ``statedir`` is not set, this directory is used by the
   content backing store (if applicable).  By default, each broker rank creates
   a unique ``rundir`` in ``$TMPDIR`` and removes it on exit.  If ``rundir`` is
   set on the command line, beware exceeding the UNIX domain socket path limit
   described in :linux:man7:`unix`, as low as 92 bytes on some systems.  To
   support the :man1:`flux-start` ``--test-size`` option where multiple brokers
   share a ``rundir``, if ``rundir`` is set to a pre-existing directory, the
   directory is not removed by the broker on exit.  In most cases this
   attribute should not be set by users.

statedir [Updates: C]
   A directory in which persistent state is stored by the Flux broker.  For
   example, content backing store data is stored here to facilitate restarts.
   If unset, this data goes to ``rundir`` where it is cleaned up on instance
   shutdown.  If set, this directory must exist and be owned by the instance
   owner.  Default: unset.

security.owner
   The numeric userid of the owner of this Flux instance.

local-uri [Updates: C]
   The Flux URI that the local connector binds to for accepting connections
   from local Flux clients.  The name must begin with ``local://``
   and the path must refer to a :linux:man7:`unix` domain socket in an
   existing directory.

parent-uri
   The value of the broker's ``FLUX_URI`` environment variable.  This is the
   URI that should be passed to :man3:`flux_open` to establish a connection to
   the enclosing instance.

instance-level
   The nesting level of this Flux instance, or ``0`` if there is no enclosing
   Flux instance.

jobid
   The Flux job ID of this Flux instance, if it was launched by Flux as a job.
   The value is obtained from ``PMI_KVS_Get_my_name()`` which may be something
   other than a Flux job ID if Flux was started by another means.

parent-kvs-namespace
   The value of the broker's ``FLUX_KVS_NAMESPACE`` environment variable.
   This is the KVS namespace assigned to this Flux instance by its enclosing
   instance, if it was launched by Flux as a job.

hostlist
   An RFC 29 hostlist in broker rank order.  This value may be used to
   translate between broker ranks and hostnames.

broker.mapping
   A string representing the process mapping of broker ranks in the Flux
   Task Map format described in RFC 34.  For example, ``[[0,16,1,1]]`` means
   the instance has one broker per node on 16 nodes, and ``[[0,1,16,1]]``
   means it has 16 brokers on one node.

broker.critical-ranks [Updates: C]
   An RFC 22 idset representing broker ranks that are considered critical
   to instance operation. The broker notifies the job execution system in
   the parent instance of these ranks such that a fatal job exception
   is raised when a failing node or other error occurs affecting any rank
   in this set. Default: rank 0 plus any other overlay network routers.

broker.boot-method [Updates: C]
   A URI representing the method used to bootstrap Flux.  Valid values are
   ``config`` (boot via TOML config file), ``simple`` (use the PMI-1 simple
   wire protocol), ``libpmi[:path]`` (use a PMI-1 shared library), or
   ``single`` (standalone size=1).  Additional boot methods may be provided
   by plugins.

broker.pid
   The process id of the local broker.

broker.quorum [Updates: C]
   The number of brokers that are required to be online before the rank 0
   broker enters the RUN state and starts the initial program, if any.
   Default: instance size.

broker.quorum-timeout [Updates: C]
   The amount of time (in RFC 23 Flux Standard Duration format) that the
   rank 0 broker waits for the ``broker.quorum`` set to come online before
   aborting the Flux instance.   Default: ``60s``.

broker.rc1_path [Updates: C]
   The path to the broker's rc1 script.  Default: ``${prefix}/etc/flux/rc1``.

broker.rc3_path [Updates: C]
   The path to the broker's rc3 script.  Default: ``${prefix}/etc/flux/rc1``.

broker.exit-restart [Updates: C, R]
   A numeric exit code that the broker uses to indicate that it should not be
   restarted.  This is set by the systemd unit file.  Default: unset.

broker.starttime
   Timestamp of broker startup from :man3:`flux_reactor_now`.

conf.connector_path
   The value of the broker's ``FLUX_CONNECTOR_PATH`` environment variable.

conf.exec_path
   The value of the broker's ``FLUX_EXEC_PATH`` environment variable.

conf.module_path
   The value of the broker's ``FLUX_MODULE_PATH`` environment variable.

conf.pmi_library_path
   The value of the broker's ``FLUX_PMI_LIBRARY_PATH`` environment variable.

conf.shell_initrc [Updates: C, R]
   The path to the :man1:`flux-shell` initrc script.  Default:
   ``${prefix}/etc/flux/shell/initrc.lua``.

conf.shell_pluginpath [Updates: C, R]
   The list of colon-separated directories to be searched by :man1:`flux-shell`
   for shell plugins.  Default: ``${prefix}/lib/flux/shell/plugins``.

config.path [Updates: see below]
   A config file or directory (containing ``*.toml`` config files) for
   this Flux instance. This attribute may be set via the FLUX_CONF_DIR
   environment variable, or the :man1:`flux-broker` ``--config-path``
   command line argument.  Default: none.  See also :man5:`flux-config`.


TREE BASED OVERLAY NETWORK
==========================

tbon.topo [Updates: C]
   URI describing the TBON tree topology such as ``kary:16``.  The ``kary``
   scheme selects a complete, k-ary tree with fanout *k*, with ``kary:0``
   meaning that rank 0 is the parent of all other ranks by convention.  The
   ``binomial`` scheme selects a binomial tree topology of the minimum order
   that fits the instance size.  Default: ``kary:2``, unless bootstrapping by
   TOML configuration, then see :man5:`flux-config-bootstrap`.

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

tbon.zmqdebug [Updates: C]
   If set to an non-zero integer value, 0MQ socket event logging is enabled,
   if available.  This is potentially useful for debugging overlay
   connectivity problems.  Default: ``0``.

tbon.prefertcp [Updates: C]
   If set to an integer value other than zero, and the broker is bootstrapping
   with PMI, tcp:// endpoints will be used instead of ipc://, even if all
   brokers are on a single node.  Default: ``0``.

tbon.torpid_min [Updates: C, R]
   The amount of time (in RFC 23 Flux Standard Duration format) that a broker
   will allow the connection to its TBON parent to remain idle before sending a
   control message to indicate create activity.  Default: ``5s``.

tbon.torpid_max [Updates: C, R]
   The amount of time (in RFC 23 Flux Standard Duration format) that a broker
   will wait for an idle TBON child connection to send messages before
   declaring it torpid (unresponsive).  A value of 0 disables torpid node
   checking.  Torpid nodes are automatically drained and require manual
   undraining with :man1:`flux-resource`.  Default: ``30s``.

tbon.tcp_user_timeout
   The amount of time (in RFC 23 Flux Standard Duration format) that a broker
   waits for a TBON child connection to acknowledge transmitted TCP data
   before forcibly closing the connection.  A value of 0 means use the system
   default.  This value affects how Flux responds to an abruptly turned off
   node, which could take up to 20m if this value is not set.  This attribute
   may not be changed during runtime.  The broker attribute overrides
   the :man5:`flux-config-tbon` ``tcp_user_timeout`` value, if configured.
   See also: :linux:man7:`tcp`, TCP_USER_TIMEOUT socket option.

tbon.connect_timeout
   The amount of time (in RFC 23 Flux Standard Duration format) that a broker
   waits for a :linux:man2:`connect` attempt to its TBON parent to succeed
   before retrying.  A value of 0 means use the system default.  This
   attribute may not be changed during runtime.  The broker attribute
   overrides the :man5:`flux-config-tbon` ``connect_timeout`` value, if
   configured.

LOGGING
=======

log-ring-size [Updates: C, R]
   The maximum number of log entries that can be stored in the ring buffer.
   Default: ``1024``.

log-forward-level [Updates: C, R]
   Log entries at :linux:man3:`syslog` level at or below this value
   are forwarded to rank zero for permanent capture.  Default ``7``
   (LOG_DEBUG).

log-critical-level [Updates: C, R]
   Log entries at :linux:man3:`syslog` level at or below this value
   are copied to stderr on the logging rank, for capture by the
   enclosing instance.  Default ``2`` (LOG_CRIT).

log-filename [Updates: C, R]
   (rank zero only) If set, session log entries, as filtered by
   ``log-forward-level``, are directed to this file.  Default: none.

log-stderr-mode [Updates: C, R]
   If set to "leader" (default), broker rank 0 emits forwarded logs from
   other ranks to stderr, subject to the constraints of log-forward-level
   and log-stderr-level.  If set to "local", each broker emits its own
   logs to stderr, subject to the constraints of log-stderr-level.
   Default: ``leader``.

log-stderr-level (Updates: C, R)
   Log entries at :linux:man3:`syslog` level at or below this value to
   stderr, subject to log-stderr-mode.  Default: ``3`` (LOG_ERR).

log-level (Updates: C, R)
   Log entries at :linux:man3:`syslog` level at or below this value
   are stored in the ring buffer.  Default: ``7`` (LOG_DEBUG).


CONTENT
=======

content.backing-module (Updates: C)
   The selected backing store module, if any. This attribute is only set
   on rank 0 where the content backing store is active.  Default:
   ``content-sqlite``.

content.blob-size-limit (Updates: C, R)
   The maximum size of a blob, the basic unit of content storage.
   Default: ``1073741824``.

content.flush-batch-limit (Updates: C, R)
   The maximum number of outstanding store requests that will be initiated
   when handling a flush or backing store load operation.  Default: ``256``.

content.hash (Updates: C)
   The selected hash algorithm.  Default ``sha1``.  Other options: ``sha256``.

content.purge-old-entry (Updates: C, R)
   When the cache size footprint needs to be reduced, only consider purging
   entries that are older than this number of seconds.  Default:  ``10``.

content.purge-target-size (Updates: C, R)
   If possible, the cache size purged periodically so that the total size of
   the cache stays at or below this value, in bytes.  Default: ``16777216``.


RESOURCES
=========

Flux: http://flux-framework.org

RFC 13: Simple Process Manager Interface v1: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_13.html

RFC 22: Idset String Representation: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_22.html

RFC 23: Flux Standard Duration: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_23.html

RFC 29: Hostlist Format: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_29.html


SEE ALSO
========

:man1:`flux-broker`, :man1:`flux-getattr`, :man3:`flux_attr_get`
