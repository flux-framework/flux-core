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

curve_cert
   (optional) Path to a CURVE certificate generated with flux-keygen(1).
   The certificate should be identical on all broker ranks.
   It is required for instance sizes > 1.

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

Since it would be tedious to repeat host entries for every compute
node in a large cluster, the ``hosts`` array may be abbreviated using
RFC29 hostlists.  For example, the list of hosts foo0, foo1, foo2,
foo3, foo18, foo4, foo20 can be represented as "foo[0-3,18,4,20]".


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
