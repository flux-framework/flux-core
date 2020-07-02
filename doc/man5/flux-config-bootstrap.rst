========================
flux-config-bootstrap(5)
========================


DESCRIPTION
===========

The broker discovers the size of the Flux instance, the broker's rank,
and overlay network wireup information either dynamically using a PMI
service, such as when being launched by Flux or another resource manager,
or statically using the ``bootstrap`` section of the Flux configuration,
such as when being launched by systemd.

The default bootstrap mode is PMI. To select config file bootstrap,
specify the config directory with the ``--config-path=PATH`` broker command
line option or set ``FLUX_CONF_DIR`` in the broker's environment. Ensure that
this directory contains a file that defines the ``bootstrap`` section.


CONFIG FILES
============

Flux uses the TOML configuration file format. The ``bootstrap`` section is
a TOML table containing the following keys. Each node in a cluster is
expected to bootstrap from an identical config file.


KEYWORDS
========

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


COMPACT HOSTS
=============

Since it would be tedious to repeat host entries for every compute node in
a large cluster, the ``hosts`` array may be abbreviated using bracketed
"idset" notation in ``host`` keys.

An idset is an unordered set of non-negative integers that may be expressed
as a comma-separated list including hyphenated ranges. For example
the set 0, 1, 2, 3, 4, 18, 20 may be represented as "0-4,18,20".

A ``host`` key may include one or more bracketed idsets. For example,
"foo[0-1023]" represents the hosts "foo0, foo1, …​, foo1023", or
"rack[0-1]node[0-1]" represents the hosts "rack0node0, rack0node1,
rack1node0, rack1node1".


EXAMPLE
=======

::

   [bootstrap]

   default_port = 8050
   default_bind = "tcp://en0:%p"
   default_connect = "tcp://e%h:%p"

   hosts = [
       {
           host="fluke0",
           bind="tcp://en4:9001",
           connect="tcp://fluke-mgmt:9001"
       },
       { host = "fluke[1-1023]" },
   ]


RESOURCES
=========

Github: http://github.com/flux-framework


SEE ALSO
========

flux-getattr(1), flux_attr_get(3)
