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

tcp_user_timeout
   (optional) The amount of time (in RFC 23 Flux Standard Duration format) that
   a broker waits for a TBON child connection to acknowledge transmitted TCP
   data before forcibly closing the connection.  A value of 0 means use the
   system default.  This value affects how Flux responds to an abruptly turned
   off node.  The configured value may be overridden by setting the
   ``tbon.tcp_user_timeout`` broker attribute.  See also: :linux:man7:`tcp`,
   TCP_USER_TIMEOUT socket option.  Default: 20s.

zmqdebug
   (optional) Integer value indicating whether ZeroMQ socket debug logging
   should be enabled: 0=disabled, 1=enabled.  Default: ``0``.  This configured
   value may be overridden by setting the ``tbon.zmqdebug`` broker attribute.


EXAMPLE
=======

::

   [tbon]
   torpid_min = "10s"
   torpid_max = "1m"

   tcp_user_timeout = "2m"


RESOURCES
=========

Flux: http://flux-framework.org

RFC 23: Flux Standard Duration: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_23.html


SEE ALSO
========

:man5:`flux-config`, :man7:`flux-broker-attributes`
