============
flux-ping(1)
============


SYNOPSIS
========

**flux** **ping** [*OPTIONS*] target


DESCRIPTION
===========

flux-ping(1) measures round-trip latency to a Flux service implementing
the "ping" method in a manner analogous to ping(8). The ping response is
essentially an echo of the request, with the route taken to the service
added by the service. This route is displayed in the output and can
give insight into how various addresses are routed.

*target* may be the name of a Flux service, e.g. "kvs".
flux-ping(1) will send a request to "kvs.ping". As a shorthand,
*target* can include a rank or host prefix delimited by an exclamation point.
"flux ping 4!kvs" is equivalent to "flux ping --rank 4 kvs" (see --rank
option below). Don't forget to quote the exclamation point if it is
interpreted by your shell.

As a shorthand, *target* may also simply be a rank or host by itself
indicating that the broker on that rank/host, rather than a Flux
service, is to be pinged. "flux ping 1" is equivalent to
"flux ping --rank 1 broker".


OPTIONS
=======

**-r, --rank**\ *=N*
   Find target on a specific broker rank. Special case strings “*any*”
   and “*upstream*” available to ping FLUX_NODEID_ANY and FLUX_NODEID_UPSTREAM
   respectively. Default: send to “*any*”.

**-p, --pad**\ *=N*
   Include in the payload a string of length *N* bytes. *N* may be a
   floating point number with optional multiplicative suffix k,K=1024,
   M=1024\*1024, or G=1024\*1024\*1024. The payload will be echoed back in
   the response. This option can be used to explore the effect of message
   size on latency. Default: no padding.

**-i, --interval**\ *=N*
   Specify the delay, in seconds, between successive requests.
   A value of zero is valid and indicates that there should be no delay.
   Requests are sent without waiting for responses. Default: 1.0 seconds.

**-c, --count**\ *=N*
   Specify the number of requests to send, and terminate the command once
   responses have been received for all the requests. Default: unlimited.

**-b, --batch**
   Begin processing responses after all requests are sent. Requires --count.

**-u, --userid**
   Include userid and rolemask of original request, which are echoed back
   in ping response, in ping output.


EXAMPLES
========

One can ping a service by name, e.g.

::

   $ flux ping kvs
   kvs.ping pad=0 seq=0 time=0.774 ms (0EB02!A3368!0!382A6)
   kvs.ping pad=0 seq=1 time=0.686 ms (0EB02!A3368!0!382A6)
   ...

This tells you that the local "kvs" service is alive and the
round-trip latency is a bit over half a millisecond. The route hops are:

::

   0EB02: UUID of the ping command
   A3368: UUID of the API module
   0:     rank of the local broker
   382A6: UUID of the KVS module.


RESOURCES
=========

Flux: http://flux-framework.org
