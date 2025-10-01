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
   By default, each broker rank creates
   a unique temporary directory and removes it on exit.  If ``rundir`` is
   set on the command line, beware exceeding the UNIX domain socket path limit
   described in :linux:man7:`unix`, as low as 92 bytes on some systems.  To
   support the :man1:`flux-start` ``--test-size`` option where multiple brokers
   share a ``rundir``, if ``rundir`` is set to a pre-existing directory, the
   directory is not removed by the broker on exit.  In most cases this
   attribute should not be set by users.

rundir-cleanup [Updates: C]
   This attribute overrides the default ``rundir`` cleanup described above.
   If set to ``1`` the directory is removed on broker exit.
   If set to ``0`` the directory is not removed.

statedir [Updates: C]
   A directory in which persistent state is stored by the Flux leader broker.
   For example, content backing store data is stored here to facilitate
   restarts.  If unset, a unique temporary directory is created.
   If set, and the directory exists, it must be owned by the instance owner.
   If set, and the directory does not exist, it is created.  If the broker
   created the directory, then the directory is removed by the broker on exit.
   The state directory is only used on the leader (rank 0) broker.

statedir-cleanup [Updates: C]
   This attribute overrides the default ``statedir`` cleanup described above.
   If set to ``1`` the directory is removed on broker exit.
   If set to ``0`` the directory is not removed.

.. note::
   If ``statedir`` or ``rundir`` is set on the command line, and the
   specified directory has the sticky bit set, it is assumed to be a ``/tmp``
   like directory, and the broker creates a unique temporary directory within
   that directory.

security.owner
   The numeric userid of the owner of this Flux instance.

local-uri [Updates: C]
   The Flux URI that the local connector binds to for accepting connections
   from local Flux clients.  The name must begin with ``local://``
   and the path must refer to a :linux:man7:`unix` domain socket in an
   existing directory.

parent-uri
   The URI that should be passed to :man3:`flux_open` to establish a connection
   to the enclosing instance.

instance-level
   The nesting level of this Flux instance, or ``0`` if there is no enclosing
   Flux instance.

jobid
   The Flux job ID of this Flux instance, if it was launched by Flux as a job.
   The value is obtained from ``PMI_KVS_Get_my_name()`` which may be something
   other than a Flux job ID if Flux was started by another means.

parent-kvs-namespace
   The value of the broker's :envvar:`FLUX_KVS_NAMESPACE` environment variable.
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

broker.quorum-warn [Updates: C]
   The amount of time (in RFC 23 Flux Standard Duration format) that the
   rank 0 broker waits for the ``broker.quorum`` set to come online before
   warning of slow joiners.   Default: ``60s``.

broker.shutdown-warn [Updates: C]
   During shutdown, the amount of time (in RFC 23 Flux Standard Duration
   format) that a broker waits for its TBON children to disconnect before
   warning of slow peers.  Default: ``60s``.

broker.cleanup-timeout [Updates: C]
   The amount of time (in RFC 23 Flux Standard Duration format) that the
   rank 0 broker waits for cleanup actions to complete when the broker has
   received a terminating signal.  Default: ``none``.

broker.rc1_path [Updates: C]
   The command line executed by the broker for rc1.
   Default: ``flux modprobe rc1``.

broker.rc3_path [Updates: C]
   The command line executed by the broker for rc3.
   Default: ``flux modprobe rc3``.

broker.rc2_none [Updates: C]
   If set, do not run an initial program.

broker.rc2_pgrp [Updates: C]
   By default, rc2 will be placed in the same process group as the
   broker whenever the broker is itself a process group leader. If the
   ``broker.rc2_pgrp`` attribute is set, then rc2 will always be placed
   in its own process group.

broker.exit-restart [Updates: C, R]
   A numeric exit code that the broker uses to indicate that it should not be
   restarted.  This is set by the systemd unit file.  Default: unset.

broker.module-nopanic [Updates: C, R]
   By default, when a broker module spuriously exits with error, the broker
   shuts down its subtree and fails.  If this attribute is set, this event
   is merely logged.

broker.starttime
   Timestamp of broker startup from :man3:`flux_reactor_now`.

broker.sd-notify
   A boolean indicating that the broker should use :linux:man3:`sd_notify`
   to inform systemd of its status.  This is set to 1 in the Flux systemd
   unit file.

broker.sd-stop-timeout
   A timeout value (in RFC 23 Flux Standard Duration format) used by the
   broker to extend the systemd stop timeout while it is making progress
   towards shutdown.  This is set to the same value as ``TimeoutStopSec``
   in the Flux systemd unit file.

conf.shell_initrc [Updates: C, R]
   The path to the :man1:`flux-shell` initrc script.  Default:
   ``${sysconfdir}/flux/shell/initrc.lua``.

conf.shell_pluginpath [Updates: C, R]
   The list of colon-separated directories to be searched by :man1:`flux-shell`
   for shell plugins.  Default: ``${libdir}/flux/shell/plugins``.

config.path [Updates: see below]
   A config file or directory (containing ``*.toml`` config files) for
   this Flux instance. This attribute may be set via the :envvar:`FLUX_CONF_DIR`
   environment variable, or the :man1:`flux-broker` ``--config-path``
   command line argument.  Default: none.  See also :man5:`flux-config`.


TREE BASED OVERLAY NETWORK
==========================

tbon.topo [Updates: C]
   A URI describing the TBON tree topology.  The following schemes are
   available:

   kary:k
      A complete, k-ary tree with fanout *k*.  By convention, ``kary:0``
      indicates that rank 0 is the parent of all other ranks.

   mincrit[:k]
      Minimize critical ranks.  The tree height is limited to three.
      If *k* is specified, it sets the number of router (interior) ranks,
      making the number of critical ranks *k* plus one.  The fanout from
      routers to leaves is determined by the instance size.  If *k* is
      unspecified, *k* is set to a value that avoids exceeding a fanout of
      1024 at any level.  If that cannot be achieved in three levels,
      then rank 0 is overloaded.

   binomial
      Binomial tree topology of the minimum order that fits the instance size.

   custom
      The topology is set by TOML configuration.
      See :man5:`flux-config-bootstrap`.

   The default value is ``kary:32``.

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

tbon.zmq_io_threads [Updates: C]
   Set the number of I/O threads libzmq will start on the leader node.
   Default: ``1``.

tbon.child_rcvhwm [Updates: C]
   Limit the number of messages stored locally on behalf of each downstream
   TBON peer.  When the limit is reached, messages are queued on the peer
   instead.  Default: ``0`` (unlimited).

tbon.prefertcp [Updates: C]
   If set to an integer value other than zero, and the broker is bootstrapping
   with PMI, tcp:// endpoints will be used instead of ipc://, even if all
   brokers are on a single node.  Default: ``0``.

tbon.interface-hint [Updates: C, R]
   When bootstrapping with PMI, tcp endpoints are chosen heuristically
   using one of the following methods:

   default-route
      The address associated with the default route (default, but see below).
   hostname
      The address associated with the system hostname.
   *interface*
     The address associated with the named network interface, e.g. ``enp4s0``
   *network*
     The address associated with the first interface that matches the
     network address in CIDR form, e.g. ``10.0.2.0/24``.  NOTE: IPv6
     network addresses are not supported at this time.

   If the attribute is not explicitly set, its value is assigned from
   (in descending precedence):

   1. the TOML configuration key of the same name
   2. the :envvar:`FLUX_IPADDR_INTERFACE` or :envvar:`FLUX_IPADDR_HOSTNAME`
      environment variables (these should be considered deprecated)
   3. the enclosing Flux instance's value (via the PMI KVS)
   4. the compiled-in default of default-route

tbon.torpid_min [Updates: C, R]
   The amount of time (in RFC 23 Flux Standard Duration format) that a broker
   will allow the connection to its TBON parent to remain idle before sending a
   control message to indicate create activity.  Default: ``5s``.

tbon.torpid_max [Updates: C, R]
   The amount of time (in RFC 23 Flux Standard Duration format) that a broker
   will wait for an idle TBON child connection to send messages before
   declaring it torpid (unresponsive).  A value of 0 disables torpid node
   checking.  New work is not scheduled on a node while torpid, but a job
   running on a node when it becomes torpid is allowed to complete.
   Default: ``30s``.

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
   Log entries of numerical severity level less than or equal to this value
   are forwarded to rank zero for permanent capture.  Default: ``7``.

log-critical-level [Updates: C, R]
   Log entries of numerical severity level less than or equal to this value
   are copied to stderr on the logging rank, for capture by the
   enclosing instance.  Default: ``2``.

log-filename [Updates: C, R]
   (rank zero only) If set, session log entries, as filtered by
   ``log-forward-level``, are directed to this file.  Default: none.

log-syslog-enable [Updates: C, R]
   If set to 1, each broker emits logs to syslog.  Default: none.

log-syslog-level [Updates: C, R]
   Log entries of numerical severity level less than or equal to this value
   are emitted to syslog.  Default: ``2``.

log-stderr-mode [Updates: C, R]
   If set to "leader" (default), broker rank 0 emits forwarded logs from
   other ranks to stderr, subject to the constraints of log-forward-level
   and log-stderr-level.  If set to "local", each broker emits its own
   logs to stderr, subject to the constraints of log-stderr-level.
   Default: ``leader``.

log-stderr-level (Updates: C, R)
   Log entries of numerical severity level less than or equal to this value to
   stderr, subject to log-stderr-mode.  Default: ``3``.

log-level (Updates: C, R)
   Log entries of numerical severity level less than or equal to this value
   are stored in the ring buffer.  Default: ``7``.

The numerical severity levels are defined as per :linux:man3:`syslog`:

.. list-table::
   :header-rows: 1

   * - 0
     - :const:`LOG_EMERG`

   * - 1
     - :const:`LOG_ALERT`

   * - 2
     - :const:`LOG_CRIT`

   * - 3
     - :const:`LOG_ERR`

   * - 4
     - :const:`LOG_WARNING`

   * - 5
     - :const:`LOG_NOTICE`

   * - 6
     - :const:`LOG_INFO`

   * - 7
     - :const:`LOG_DEBUG`

CONTENT
=======

content.backing-module (Updates: C)
   The selected backing store module, if any. This attribute is only set
   on rank 0 where the content backing store is active.  Default:
   ``content-sqlite``.

content.hash (Updates: C)
   The selected hash algorithm.  Default ``sha1``.  Other options: ``sha256``.


RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_13`

:doc:`rfc:spec_22`

:doc:`rfc:spec_23`

:doc:`rfc:spec_29`


SEE ALSO
========

:man1:`flux-broker`, :man1:`flux-getattr`, :man3:`flux_attr_get`
