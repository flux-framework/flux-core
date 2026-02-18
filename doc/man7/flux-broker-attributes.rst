=========================
flux-broker-attributes(7)
=========================


DESCRIPTION
===========

Flux broker attributes are broker parameters with a scope of a single broker
rank.  They may be listed with :man1:`flux-lsattr` and queried with
:man1:`flux-getattr`.

Many attributes may be set on the command line when Flux is started, e.g.
with :option:`flux start --setattr` or :option:`flux broker --setattr`.
This should be assumed to be possible unless marked otherwise.

Attributes that are strictly informational and not meant to be set on the
command line or at runtime are tagged below with
:ref:`[readonly] <attr_readonly>`

Attributes that override TOML configuration values when set on the command
line are tagged below with :ref:`[config] <attr_config>`

Attributes that may be updated at runtime with :man1:`flux-setattr` are
tagged with :ref:`[runtime] <attr_runtime>`.


GENERAL
=======

rank :ref:`[readonly] <attr_readonly>`
   The rank of the local broker.

size :ref:`[readonly] <attr_readonly>`
   The number of broker ranks in the flux instance

version :ref:`[readonly] <attr_readonly>`
   The version of flux-core that was used to build this broker.

rundir
   A temporary directory where the broker's UNIX domain sockets are located.
   By default, each broker rank creates
   a unique temporary directory and removes it on exit.  If ``rundir`` is
   set on the command line, beware exceeding the UNIX domain socket path limit
   described in :linux:man7:`unix`, as low as 92 bytes on some systems.  To
   support the :man1:`flux-start` ``--test-size`` option where multiple brokers
   share a ``rundir``, if ``rundir`` is set to a pre-existing directory, the
   directory is not removed by the broker on exit.  In most cases this
   attribute should not be set by users.

rundir-cleanup
   This attribute overrides the default ``rundir`` cleanup described above.
   If set to ``1`` the directory is removed on broker exit.
   If set to ``0`` the directory is not removed.

statedir
   A directory in which persistent state is stored by the Flux leader broker.
   For example, content backing store data is stored here to facilitate
   restarts.  If unset, a unique temporary directory is created.
   If set, and the directory exists, it must be owned by the instance owner.
   If set, and the directory does not exist, it is created.  If the broker
   created the directory, then the directory is removed by the broker on exit.
   The state directory is only used on the leader (rank 0) broker.

statedir-cleanup
   This attribute overrides the default ``statedir`` cleanup described above.
   If set to ``1`` the directory is removed on broker exit.
   If set to ``0`` the directory is not removed.

.. note::
   If ``statedir`` or ``rundir`` is set on the command line, and the
   specified directory has the sticky bit set, it is assumed to be a ``/tmp``
   like directory, and the broker creates a unique temporary directory within
   that directory.

security.owner :ref:`[readonly] <attr_readonly>`
   The numeric userid of the owner of this Flux instance.

local-uri
   The Flux URI that the local connector binds to for accepting connections
   from local Flux clients.  The name must begin with ``local://``
   and the path must refer to a :linux:man7:`unix` domain socket in an
   existing directory.

parent-uri :ref:`[readonly] <attr_readonly>`
   The URI that should be passed to :man3:`flux_open` to establish a connection
   to the enclosing instance.

instance-level :ref:`[readonly] <attr_readonly>`
   The nesting level of this Flux instance, or ``0`` if there is no enclosing
   Flux instance.

jobid :ref:`[readonly] <attr_readonly>`
   The Flux job ID of this Flux instance, if it was launched by Flux as a job.
   The value is obtained from ``PMI_KVS_Get_my_name()`` which may be something
   other than a Flux job ID if Flux was started by another means.

jobid-path :ref:`[readonly] <attr_readonly>`
   A ``/``-separated list of job IDs representing the Flux instance hierarchy.
   The top level Flux instance, which has no Flux job ID, is represented
   as ``/``, similar to UNIX directories.

parent-kvs-namespace :ref:`[readonly] <attr_readonly>`
   The value of the broker's :envvar:`FLUX_KVS_NAMESPACE` environment variable.
   This is the KVS namespace assigned to this Flux instance by its enclosing
   instance, if it was launched by Flux as a job.

hostlist
   An RFC 29 hostlist in broker rank order.  This value may be used to
   translate between broker ranks and hostnames.

hostname
   The system hostname.  This attribute is set on the broker command line by
   :option:`flux start --test-hosts`, which is useful in certain test
   scenarios.

broker.mapping :ref:`[readonly] <attr_readonly>`
   A string representing the process mapping of broker ranks in the Flux
   Task Map format described in RFC 34.  For example, ``[[0,16,1,1]]`` means
   the instance has one broker per node on 16 nodes, and ``[[0,1,16,1]]``
   means it has 16 brokers on one node.

broker.critical-ranks
   An RFC 22 idset representing broker ranks that are considered critical
   to instance operation. The broker notifies the job execution system in
   the parent instance of these ranks such that a fatal job exception
   is raised when a failing node or other error occurs affecting any rank
   in this set. Default: rank 0 plus any other overlay network routers.

broker.boot-method
   A URI representing the method used to bootstrap Flux.  Valid values are
   ``config`` (boot via TOML config file), ``simple`` (use the PMI-1 simple
   wire protocol), ``libpmi[:path]`` (use a PMI-1 shared library), or
   ``single`` (standalone size=1).  Additional boot methods may be provided
   by plugins.

broker.pid :ref:`[readonly] <attr_readonly>`
   The process id of the local broker.

broker.quorum
   The number of brokers that are required to be online before the rank 0
   broker enters the RUN state and starts the initial program, if any.
   Default: instance size.

broker.quorum-warn
   The amount of time (in RFC 23 Flux Standard Duration format) that the
   rank 0 broker waits for the ``broker.quorum`` set to come online before
   warning of slow joiners.   Default: ``60s``.

broker.shutdown-warn
   During shutdown, the amount of time (in RFC 23 Flux Standard Duration
   format) that a broker waits for its TBON children to disconnect before
   warning of slow peers.  Default: ``60s``.

broker.shutdown-timeout
   During shutdown, the amount of time (in RFC 23 Flux Standard Duration
   format) that a broker waits for its TBON children to disconnect before
   giving up and moving on to the next state.  Default: ``none``.

broker.cleanup-timeout
   The amount of time (in RFC 23 Flux Standard Duration format) that the
   rank 0 broker waits for cleanup actions to complete when the broker has
   received a terminating signal.  Default: ``none``.

broker.rc1_path
   The command line executed by the broker for rc1.
   Default: ``flux modprobe rc1``.

broker.rc3_path
   The command line executed by the broker for rc3.
   Default: ``flux modprobe rc3``.

broker.rc2_none
   If set, do not run an initial program.

broker.rc2_pgrp
   By default, rc2 will be placed in the same process group as the
   broker whenever the broker is itself a process group leader. If the
   ``broker.rc2_pgrp`` attribute is set, then rc2 will always be placed
   in its own process group.

broker.exit-restart :ref:`[runtime] <attr_runtime>`
   A numeric exit code that the broker uses to indicate that it should not be
   restarted.  This is set by the systemd unit file.  Default: unset.

broker.module-nopanic :ref:`[runtime] <attr_runtime>`
   By default, when a broker module spuriously exits with error, the broker
   shuts down its subtree and fails.  If this attribute is set, this event
   is merely logged.

broker.starttime :ref:`[readonly] <attr_readonly>`
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

broker.exit-norestart
   Set the exit code to be used to indicate to systemd that Flux should
   not be restarted.  The systemd unit file sets
   :option:`RestartPreventExitStatus` and :option:`SuccessExitStatus`
   to this value.

broker.recovery-mode
   This attribute is set by :option:`flux-start --recovery` as a flag to
   the system that recovery is in progress.  For example, in recovery mode,
   rc1 errors are non-fatal and downstream TBON connections are prevented.

broker.uuid :ref:`[readonly] <attr_readonly>`
   The local broker UUID, used for request/response message routing.

conf.shell_initrc :ref:`[runtime] <attr_runtime>`
   The path to the :man1:`flux-shell` initrc script.  Default:
   ``${sysconfdir}/flux/shell/initrc.lua``.

conf.shell_pluginpath :ref:`[runtime] <attr_runtime>`
   The list of colon-separated directories to be searched by :man1:`flux-shell`
   for shell plugins.  Default: ``${libdir}/flux/shell/plugins``.

config.path
   A config file or directory (containing ``*.toml`` config files) for
   this Flux instance. This attribute may be set via the :envvar:`FLUX_CONF_DIR`
   environment variable, or the :man1:`flux-broker` ``--config-path``
   command line argument.  Default: none.  See also :man5:`flux-config`.

TREE BASED OVERLAY NETWORK
==========================

tbon.topo :ref:`[config] <attr_config>`
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

tbon.descendants :ref:`[readonly] <attr_readonly>`
   Number of descendants "below" this node of the tree based
   overlay network, not including this node.

tbon.level :ref:`[readonly] <attr_readonly>`
   The level of this node in the tree based overlay network.
   Root is level 0.

tbon.maxlevel :ref:`[readonly] <attr_readonly>`
   The maximum level number in the tree based overlay network.
   Maxlevel is 0 for a size=1 instance.

tbon.endpoint :ref:`[readonly] <attr_readonly>`
   The ZeroMQ endpoint of this broker.

tbon.parent-endpoint :ref:`[readonly] <attr_readonly>`
   The ZeroMQ endpoint of this broker's TBON parent.

tbon.zmqdebug :ref:`[config] <attr_config>`
   If set to an non-zero integer value, 0MQ socket event logging is enabled,
   if available.  This is potentially useful for debugging overlay
   connectivity problems.  Default: ``0``.

tbon.zmq_io_threads :ref:`[config] <attr_config>`
   Set the number of I/O threads libzmq will start on the leader node.
   Default: ``1``.

tbon.child_rcvhwm :ref:`[config] <attr_config>`
   Limit the number of messages stored locally on behalf of each downstream
   TBON peer.  When the limit is reached, messages are queued on the peer
   instead.  Default: ``0`` (unlimited).

tbon.prefertcp
   If set to an integer value other than zero, and the broker is bootstrapping
   with PMI, tcp:// endpoints will be used instead of ipc://, even if all
   brokers are on a single node.  Default: ``0``.

tbon.interface-hint :ref:`[config] <attr_config>` :ref:`[runtime] <attr_runtime>`
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

   This attribute is inherited by sub-instances.  Changing it at runtime
   affects new sub-instances, but not the current instance.

tbon.torpid_min :ref:`[config] <attr_config>`
   The amount of time (in RFC 23 Flux Standard Duration format) that a broker
   will allow the connection to its TBON parent to remain idle before sending a
   control message to indicate create activity.  Default: ``5s``.

tbon.torpid_max :ref:`[config] <attr_config>`
   The amount of time (in RFC 23 Flux Standard Duration format) that a broker
   will wait for an idle TBON child connection to send messages before
   declaring it torpid (unresponsive).  A value of 0 disables torpid node
   checking.  New work is not scheduled on a node while torpid, but a job
   running on a node when it becomes torpid is allowed to complete.
   Default: ``30s``.

.. note::

   The ``tbon.torpid_min`` and ``tbon.torpid_max`` values can be updated
   on a live system by updating the configuration using :man1:`flux-config`.
   The relevant configuration table is described in :man5:`flux-config-tbon`.
   In older releases, they could be updated using :man1:`flux-setattr`, but
   that is no longer true.

tbon.tcp_user_timeout :ref:`[config] <attr_config>`
   The amount of time (in RFC 23 Flux Standard Duration format) that a broker
   waits for a TBON child connection to acknowledge transmitted TCP data
   before forcibly closing the connection.  A value of 0 means use the system
   default.  This value affects how Flux responds to an abruptly turned off
   node, which could take up to 20m if this value is not set.  This attribute
   may not be changed during runtime.  See also: :linux:man7:`tcp`,
   TCP_USER_TIMEOUT socket option.

tbon.connect_timeout :ref:`[config] <attr_config>`
   The amount of time (in RFC 23 Flux Standard Duration format) that a broker
   waits for a :linux:man2:`connect` attempt to its TBON parent to succeed
   before retrying.  A value of 0 means use the system default.  This
   attribute may not be changed during runtime.

LOGGING
=======

The broker log service is described in the :ref:`broker_logging` section of
:man1:`flux-broker`.  The following attributes affect log disposition.
Attributes ending in "-level" are a numerical severity threshold, which
matches log messages of equal and lesser (more severe) value.  There is
a table of severity names vs numbers in the aforementioned description.
Negative severity values can be used to indicate "match nothing".

log-ring-size
   The maximum number of log messages that can be stored in the local
   ring buffer.  Default: 1024.

log-forward-level
   Forward matching messages to the leader broker.  This is only helpful when
   :option:`log-stderr-mode` is set to "leader", or :option:`log-filename` is
   defined.  Default: 3 (LOG_ERR).

log-critical-level
   Copy matching log messages to local stderr.  This is intended to ensure
   that important messages are not lost in situations so dire that
   normal logging may be unreliable.  Default: 2 (LOG_CRIT).

log-filename
   Copy log messages to a file on the leader broker.
   Messages from follower brokers are also captured if they match
   :option:`log-forward-level`.  Default: none.

log-syslog-enable
   Copy log messages to syslog if they match :option:`log-syslog-level`.
   Default: 0.

log-syslog-level
   Sets the severity threshold for syslog, if :option:`log-syslog-enable`
   is set.  Default: 2 (LOG_CRIT).

log-stderr-mode
   Set the stderr mode to one of:

   leader
     Follower brokers forward messages that match :option:`log-forward-level`
     to the leader, and the leader logs them to stderr if they match
     :option:`log-stderr-level`.

   local
     Each broker emits its own logs to stderr, if they match
     :option:`log-stderr-level`.

   Default: leader.

log-stderr-level :ref:`[runtime] <attr_runtime>`
   Copy matching log messages to stderr.  Default: 3 (LOG_ERR).

log-level
   Allow matching messages to enter the local broker's circular buffer.
   Default: 7 (LOG_DEBUG).

CONTENT
=======

content.backing-module
   The selected backing store module, if any. This attribute is only set
   on rank 0 where the content backing store is active.  Default:
   ``content-sqlite``.

content.hash
   The selected hash algorithm.  Default ``sha1``.  Other options: ``sha256``.

content.dump
   If set to a file path, the Flux rc3 script performs a KVS dump
   to that path at Flux shutdown.  If set to ``auto``, a dump file name
   containing the date is automatically generated.  This file is normally
   placed in Flux's current working directory; however, if ``statedir`` is
   defined, the dump goes to ``${statedir}/dump`` and a ``RESTORE`` symbolic
   link is set to point to it.

content.restore
   If set to a file path, the Flux rc1 script loads a KVS dump from that
   path at Flux startup.  If set to ``auto``, the dump file pointed
   to by ``${statedir}/dump/RESTORE`` is loaded.

CRON
====

cron.directory
   If set, the Flux rc1 script loads all crontabs from this directory
   at Flux startup.


ATTRIBUTE UPDATE NOTES
======================

.. _attr_readonly:

readonly
  This attribute is strictly informational and is not meant to be set by users.

.. _attr_config:

config
  This attribute overrides a TOML configuration key with a the same name.

.. _attr_runtime:

runtime
  This attribute may be updated at runtime with :man1:`flux-setattr`.

CAVEATS
=======

The broker does not detect misspelled attributes, because it doesn't know
the set of all valid attributes.

The broker does not detect attempts to set some read-only attributes, because
it doesn't know the rules for all attributes.

There is no mechanism for Flux components outside of the broker to be
notified when attributes are updated at runtime.

Due to these limitations, TOML configuration is gradually replacing broker
attributes as the primary way to configure and update non-broker components.

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
