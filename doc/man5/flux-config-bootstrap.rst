========================
flux-config-bootstrap(5)
========================


DESCRIPTION
===========

The system instance requires that the overlay network configuration be
statically configured.  The ``bootstrap`` TOML table defines these details
for a given cluster.

.. warning::
   Although ``flux config reload`` works on a live system, bootstrap
   settings do not take effect until the next broker restart.  As such, they
   must only be changed in conjunction with a full system instance restart in
   order to avoid brokers becoming desynchronized if they are independently
   restarted before the next instance restart.

Flux brokers are interconnected in a tree topology.  A broker has zero or one
upstream peer (towards the root, rank 0) and zero or more downstream peers
(towards the leaves).  A broker passively accepts connections from its
downstream peers on its ZeroMQ `bind` endpoint , and actively connects to its
upstream peer on that broker's ZeroMQ `connect` endpoint, which is the
remote version of the peer's `bind` endpoint.

A broker determines the ranks of its peers based on the topology.  When
bootstrapping from configuration files, the default topology is ``custom``.
In a ``custom`` topology, rank 0 is the upstream peer of all other ranks,
unless ``parent`` keys are present in the ``hosts`` array to define a
different tree shape.  The default topology may be altered from ``custom``
by configuring ``tbon.topo`` as described in :man5:`flux-config-tbon`.

A broker determines its own rank by looking for its hostname in the ``hosts``
array.  The index of the first matching entry is the broker's rank.  The
information in the ``hosts`` array also provides
 - the `bind` endpoint that the broker configures to accept connections
   from downstream peers
 - the `connect` endpoint it uses to connect to its parent

Each point to point connection between brokers is authenticated and encrypted
using ZeroMQ native CURVE cryptography.  This requires a shared certificate
to bootstrap.  This certificate is stored on disk and must be protected from
access by users other than the Flux instance owner.

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
   (optional) A rank-ordered array of host entries. Each entry is a TOML
   table containing, at minimum, a ``host`` key and optionally, ``connect``
   and ``bind`` keys.  These keys are described in detail below. The array
   is required to be defined for instance sizes > 1.

HOSTS ENTRY
===========

Each ``hosts`` array entry contains the following keys:

host
   (required) The name of the host that corresponds to this entry.  The value
   must exactly match the output of the :linux:man1:`hostname` command.

connect
   (optional) The ZeroMQ endpoint that downstream peers use to connect to this
   broker.  The value may contain ``%h`` and ``%p`` tokens as described under
   ``default_connect`` above.  If the key is omitted, ``default_connect``
   is used.

bind
   (optional) The ZeroMQ endpoint that this broker uses to accept connections
   from downstream peers.  The value may contain ``%h`` and ``%p`` tokens as
   described under ``default_bind`` above.  If the key is omitted,
   ``default_bind`` is used.

parent
    (optional) The name of the host that is the upstream peer of this entry
    in the overlay network tree topology.

ZEROMQ ENDPOINTS
================

Brokers use the :linux:man7:`zmq_tcp` transport, consisting of the transport
name ``tcp://`` followed by an address.

`Bind` addresses specify interface followed by a colon and the TCP port.
The interface may be one of:

- the wild-card ``*`` meaning all available interfaces

- the primary IP address assigned to the interface (numeric only)

- the interface name

The port should be an explicit numerical port number.

`Connect` addresses specify a peer address followed by a colon and the TCP port.
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
       { host="foo", bind="tcp://eth0:9001", connect="tcp://10.0.1.1:9001" },
       { host = "bar" },
   ]


Host ``foo`` is assigned rank 0, and binds to the interface ``eth0`` port 9001.

Host ``bar`` is assigned rank 1, and connects to ``10.0.1.1`` port 9001.

The following example represents a 256 node cluster.  The management node has
a different network interface configuration compared to its peers.

::

   [bootstrap]

   curve_cert = "/etc/flux/system/curve.cert"

   default_port = 8050
   default_bind = "tcp://en0:%p"
   default_connect = "tcp://e%h:%p"

   hosts = [
       # Management requires non-default config
       { host="test0", bind="tcp://en4:9001", connect="tcp://test-mgmt:9001" },

       # Other nodes use defaults
       { host = "test[1-255]" },
   ]

The following example is a 256 node cluster that uses the ``parent`` key to
create a tree topology with three levels.

::

   [bootstrap]

   curve_cert = "/etc/flux/system/curve.cert"

   default_port = 8050
   default_bind = "tcp://en0:%p"
   default_connect = "tcp://e%h:%p"

   [[bootstrap.hosts]]
   host = "test[0-255]"
   [[bootstrap.hosts]]
   host = "test[1,128]"
   parent = "test0"
   [[bootstrap.hosts]]
   host = "test[2-127]"
   parent = "test1"
   [[bootstrap.hosts]]
   host = "test[129-255]"
   parent = "test128"


Note that the first block of hosts defines the entire tree, with nodes test0 through test255. The second block has a comma instead of a dash, indicating a group of two members (1 and 128) and not a range. The resulting tree looks like this. The numbers on the left indicate the level of the tree.

::

        ┌───────┐                          
    1)  │ test0 │                          
        └───┬───┴──────────┐               
            │              │               
        ┌───┴───┐     ┌────┴────┐          
    2)  │ test1 │     │ test128 │          
        └───┬───┘     └────┬────┘          
            │              │               
        ┌───┴─────────┐   ┌┴──────────────┐
    3)  │ test[2-127] │   │ test[129-255] │
        └─────────────┘   └───────────────┘
    
RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_29`


SEE ALSO
========

:man5:`flux-config`
