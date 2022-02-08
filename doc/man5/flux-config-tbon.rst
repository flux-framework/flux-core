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
   checking.  Torpid nodes are automatically drained and require manual
   undraining with :man1:`flux-resource`.  This configured value may be
   overridden by setting the ``tbon.torpid_max`` broker attribute.

keepalive_enable
   (optional) An integer value to disable (0) or enable (1) TCP keepalives
   on TBON child connections.  TCP keepalives are required to detect abruptly
   turned off peers that are unable to shutdown their TCP connection
   (default 1).  This configured value may be overridden by setting the
   ``tbon.keepalive_enable`` broker attribute.

keepalive_count
   (optional) The integer number of TCP keepalive packets to send to an idle
   downstream peer with no response before disconnecting it, overriding the
   system value from :linux:man8:`sysctl` ``net.ipv4.tcp_keepalive_probes``.
   This configured value may be overridden by setting the
   ``tbon.keepalive_count`` broker attribute.

keepalive_idle
   (optional) The integer number of seconds to wait for an idle downstream
   peer to send messages before beginning to send keepalive packets, overriding
   the system value from :linux:man8:`sysctl` ``net.ipv4.tcp_keepalive_time``.
   This configured value may be overridden by setting the
   ``tbon.keepalive_idle`` broker attribute.

keepalive_interval
   (optional) The integer number of seconds to wait between sending keepalive
   packets, overriding the system value from :linux:man8:`sysctl`
   ``net.ipv4.tcp_keepalive_intvl``.  This configured value may be overridden
   by setting the ``tbon.keepalive_interval`` broker attribute.


EXAMPLE
=======

::

   [tbon]
   torpid_min = 10s
   torpid_max = 1m

   keepalive_count = 12
   keepalive_interval = 10
   keepalive_idle = 30


RESOURCES
=========

Flux: http://flux-framework.org

RFC 23: Flux Standard Duration: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_23.html


SEE ALSO
========

:man5:`flux-config`, :man7:`flux-broker-attributes`
