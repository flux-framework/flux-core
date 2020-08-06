.. flux-help-command: proxy
.. flux-help-description: Create proxy environment for Flux instance

=============
flux-proxy(1)
=============


SYNOPSIS
========

**flux** **proxy** URI [command [args...]]

DESCRIPTION
===========

**flux proxy** connects to the Flux instance identified by *URI*,
then spawns a shell with FLUX_URI pointing to a local:// socket
managed by the proxy program. As long as the shell is running,
the proxy program routes messages between the instance and the
local:// socket. Once the shell terminates, the proxy program
terminates and removes the socket.

The purpose of **flux proxy** is to allow a connection to be reused,
for example where connection establishment has high latency or
requires authentication.


EXAMPLES
========

Connect to a job running on the localhost which has a FLUX_URI
of ``local:///tmp/flux-123456-abcdef/0/local`` and spawn an interactive
shell:

::

   $ flux proxy local:///tmp/flux-123456-abcdef/0/local

Connect to the same job remotely on host foo.com:

::

   $ flux proxy ssh://foo.com/tmp/flux-123456-abcdef/0/local


RESOURCES
=========

Github: http://github.com/flux-framework
