========================
flux-config-bootstrap(5)
========================


DESCRIPTION
===========

The system instance requires that the overlay network configuration be
statically configured.  The ``bootstrap`` TOML table defines these details
for a given cluster.

WARNING:  Although ``flux config reload`` works on a live system, these
settings do not take effect until the next broker restart.  As such, they
must only be changed in conjunction with a full system instance restart in
order to avoid brokers becoming desynchronized if they are independently
restarted before the next instance restart.

The ``bootstrap`` table contains the following keys:


KEYS
====

enable_ipv6
   (optional) Boolean value for enabling IPv6.  By default only IPv4 is
   enabled.  Note that setting this to true prevents binding to a named
   interface that only supports IPv4.

curve_cert
   (optional) Path to a CURVE certificate generated with
   :man1:`flux-keygen`.  The certificate should be identical on all
   broker ranks.  It is required for instance sizes > 1.  The file should
   be owned by the instance owner (e.g. `flux` user) and only readable by
   that user.

default_port
   (optional) The value is an integer port number that is substituted
   for the token ``%p`` in the other keys.

default_bind
   (optional) The value is a ZeroMQ endpoint URI that is used for host
   entries that do not explicitly set a bind address. The tokens
   ``%p`` and ``%h`` are replaced with the default port and the host
   for the current host entry.

default_connect
   (optional) The value is a ZeroMQ endpoint URI that is used for host
   entries that do not explicitly set a connect address. The tokens
   ``%p`` and ``%h`` are replaced with the default port and the host
   for the current host entry.

hosts
   (optional) A rank-ordered array of host entries. Each host entry is
   a TOML table containing at minimum the ``host`` key. The broker determines
   its rank by matching its hostname in the hosts array and taking the array
   index. An empty or missing hosts array implies a standalone (single
   broker) instance. The entry for a broker with downstream peers must
   either assign the ``bind`` key to a ZeroMQ endpoint URI, or the ``default_bind``
   URI described above is used. The entry for a broker with downstream peers
   must also either assign the ``connect`` key to a ZeroMQ endpoint URI, or
   the ``default_connect`` URI described above is used. The same ``%h`` and ``%p``
   substitutions work here as well.


ZEROMQ ENDPOINTS
================

In this context, ZeroMQ endpoint URIs normally use the :linux:man7:`zmq_tcp`
transport, consisting of the transport name ``tcp://`` followed by an address.

Bind addresses specify interface followed by a colon and the TCP port.
The interface may be one of:

- the wild-card ``*`` meaning all available interfaces

- the primary IP address assigned to the interface (numeric only)

- the interface name

The port should be an explicit numerical port number.

Connect addresses specify a peer address followed by a colon and the TCP port.
The peer address may be one of:

- the DNS name of the peer

- the IP address of the peer in its numeric representation

When specifying the ``bind`` and ``connect`` URIs for a hosts entry, ensure
that another host can use the ``connect`` URI to reach the Flux service bound
to the ``bind`` address on the host.


COMPACT HOSTS
=============

Since it would be tedious to repeat host entries for every compute
node in a large cluster, the ``hosts`` array may be abbreviated using
RFC 29 hostlists.  For example, the list of hosts foo0, foo1, foo2,
foo3, foo18, foo4, foo20 can be represented as "foo[0-3,18,4,20]".


EXAMPLE
=======

The following example is a simple, two node cluster with a fully specified
``hosts`` array.

::

   [bootstrap]

   curve_cert = "/etc/flux/system/curve.cert"

   hosts = [
       {
           host="foo",
           bind="tcp://eth0:9001",
           connect="tcp://10.0.1.1:9001"
       },
       {
           host = "bar"
       },
   ]


Host ``foo`` is assigned rank 0, and binds to the interface ``eth0`` port 9001.

Host ``bar`` is assigned rank 1, and connects to ``10.0.1.1`` port 9001.

The following example is a 1024 node cluster that relies on default settings
and compact hosts.  We assume a ``tbon.fanout`` of 2 (see
:man7:`flux-broker-attributes`).

::

   [bootstrap]

   curve_cert = "/etc/flux/system/curve.cert"

   default_port = 8050
   default_bind = "tcp://en0:%p"
   default_connect = "tcp://e%h:%p"

   hosts = [
       {   # Management requires non-default config
           host="test0",
           bind="tcp://en4:9001",
           connect="tcp://test-mgmt:9001"
       },
       {   # Other nodes use defaults
           host = "test[1-1023]"
       },
   ]


Host ``test0`` is assigned rank 0, and binds to interface ``en4`` port 9001.

Host ``test1`` is assigned rank 1, binds to interface ``en0`` port 8050,
and connects to ``test-mgmt`` port 9001.

Host ``test2`` is assigned rank 2, binds to interface ``en0`` port 8050,
and connects to ``test-mgmt`` port 9001.

Host ``test3`` is assigned rank 3, binds to interface ``en0`` port 8050,
and connects to ``etest1`` port 8050, and so on.


RESOURCES
=========

Flux: http://flux-framework.org

RFC 29: Hostlist Format: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_29.html


SEE ALSO
========

:man5:`flux-config`
