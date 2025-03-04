========================
flux-config-heartbeat(5)
========================


DESCRIPTION
===========

The Flux heartbeat service publishes periodic ``heartbeat.pulse`` messages
from the leader broker for synchronization.  Follower brokers subscribe
to these messages and may optionally force a disconnect from their overlay
network parent when they are are missed for a configurable period.

The ``heartbeat`` table may be used to tune the heartbeat service.  It may
contain the following keys:

KEYS
====

period
   (optional) The interval (in RFC 23 Flux Standard Duration format) between
   the publication of heartbeat messages.  Default: *2s*.

timeout
   (optional) The period (in RFC 23 Flux Standard Duration format) after
   which a follower broker will forcibly disconnect from its overlay network
   parent if it hasn't received a heartbeat message.  Set to *0* or *infinity*
   to disable.  Default: *5m*.

warn_thresh
  (optional) The number of missed heartbeat periods after which a warning
  message will be logged.  Default: 3.

EXAMPLE
=======

::

   [heartbeat]
   period = "5s"
   timeout = "1m"
   warn_thresh = 3

USE CASES
=========

Heartbeats may be used to synchronize Flux activities across brokers to
reduce the operating system jitter that affects some sensitive bulk-synchronous
applications.  :man3:`flux_sync_create` provides a way to invoke work that
is synchronized with the heartbeat.

.. note::
  The efficacy of heartbeats to mitigate noise is limited by the propagation
  delay of published messages through the tree based overlay network;  however,
  this may be reduced in the future with a side channel transport such as
  TCP multicast, hardware collectives, or quantum entanglement.

The heartbeat timeout may be used to work around a peculiarity of ZeroMQ,
the software layer underpinning the overlay network.  When a Flux broker
loses the TCP connection to its overlay parent without a shutdown (for example,
if the parent crashes or there is a network partition and TCP times out),
ZeroMQ tries indefinitely to re-establish the connection without informing
the broker.  The child broker remains in RUN state with any upstream RPCs
blocked until the parent returns to service, after which it is forced to
disconnect and shut down, which causes the RPCs to fail.  A heartbeat timeout
forces the broker to "fail fast", with the same net effect, but arriving
at a steady state sooner.

The effect of a follower broker shutdown depends on its role.  If it is
not a leaf node, the effect applies to its entire subtree.  In a system
instance, systemd restarts brokers that shut down this way.  Upon restart,
the brokers remain in JOIN state until the parent returns to service.
The heartbeat service is not loaded until after the parent connection is
established, so heartbeat timeouts do not apply in this phase.  In a user
allocation where brokers are not restarted, the outcome depends on whether
or not the broker is one of the *critical ranks* described in
:man7:`flux-broker-attributes`.

RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_23`


SEE ALSO
========

:man5:`flux-config`
