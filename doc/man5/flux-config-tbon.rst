===================
flux-config-tbon(5)
===================


DESCRIPTION
===========

The ``tbon`` table may be used to tune the configuration of the Flux tree-based
overlay network (TBON).

It may contain the following keys:


KEYS
====

topo
   (optional) A URI that selects a specific tree topology.  The default value
   is ``kary:32`` when bootstrapping from PMI, and ``custom`` when bootstrapping
   from configuration, as described in :man5:`flux-config-bootstrap`.
   The configured value may be overridden by setting the ``tbon.topo`` broker
   attribute.

torpid_min
   (optional) The amount of time (in RFC 23 Flux Standard Duration format) that
   a broker will allow the connection to its TBON parent to remain idle before
   sending a control message to create activity.  The default value of
   ``5s`` should be reasonable in most circumstances.  This configured value
   may be overridden by setting the ``tbon.torpid_min`` broker attribute.

torpid_max
   (optional) The amount of time (in RFC 23 Flux Standard Duration format) that
   a broker will wait for an idle TBON child connection to send messages before
   declaring it torpid  (unresponsive). A value of 0 disables torpid node
   checking.  New work is not scheduled on a node while torpid, but a job
   running on a node when it becomes torpid is allowed to complete.  This
   configured value may be overridden by setting the ``tbon.torpid_max``
   broker attribute.

tcp_user_timeout
   (optional) The amount of time (in RFC 23 Flux Standard Duration format) that
   a broker waits for a TBON child connection to acknowledge transmitted TCP
   data before forcibly closing the connection.  A value of 0 means use the
   system default.  This value affects how Flux responds to an abruptly turned
   off node.  The configured value may be overridden by setting the
   ``tbon.tcp_user_timeout`` broker attribute.  See also: :linux:man7:`tcp`,
   TCP_USER_TIMEOUT socket option.  Default: 20s.

connect_timeout
   (optional) The amount of time (in RFC 23 Flux Standard Duration format)
   that a broker waits for a :linux:man1:`connect` attempt to its TBON parent
   to succeed before before retrying.  A value of 0 means use the system
   default.  The configured value may be overridden by setting the
   ``tbon.connect_timeout`` broker attribute.  Default: 30s.

zmqdebug
   (optional) Integer value indicating whether ZeroMQ socket debug logging
   should be enabled: 0=disabled, 1=enabled.  Default: ``0``.  This configured
   value may be overridden by setting the ``tbon.zmqdebug`` broker attribute.

zmq_io_threads
   (optional) Integer value to set the number of I/O threads libzmq will start
   on the leader node.  The default is 1.  This configured value may be
   overridden by setting the ``tbon.zmq_io_threads`` broker attribute.

child_rcvhwm
   (optional) Integer value that limits the number of messages stored locally
   on behalf of each downstream TBON peer.  When the limit is reached, messages
   are queued on the peer instead.  Setting this reduces memory usage for
   nodes with a large number of downstream peers, at the expense of message
   latency.  The value should be 0 (unlimited) or >= 2.  The default is 0.
   This configured value may be overridden by setting the ``tbon.child_rcvhwm``
   broker attribute.

interface-hint
   When the broker's bind address is not explicitly configured via
   :man5:`flux-config-bootstrap`, it is chosen dynamically, influenced by
   one of the following hints:

   default-route
     The address associated with the default route (the default hint).
   hostname
     The address associated with the system hostname.
   *interface*
     The address associated with the named network interface, e.g. ``enp4s0``
   *network*
     The address associated with the first interface that matches the
     network address in CIDR form, e.g. ``10.0.2.0/24``.  NOTE: IPv6
     network addresses are not supported at this time.

   This configured value may be overridden by setting the
   ``tbon.interface-hint`` broker attribute on the command line.
   It is inherited by sub-instances spawned for batch jobs and allocations.
   Refer to :man7:`flux-broker-attributes` for more information.


EXAMPLE
=======

::

   [tbon]
   torpid_min = "10s"
   torpid_max = "1m"

   tcp_user_timeout = "2m"


RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_23`


SEE ALSO
========

:man5:`flux-config`, :man7:`flux-broker-attributes`
