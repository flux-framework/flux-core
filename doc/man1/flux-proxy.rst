.. flux-help-command: proxy
.. flux-help-description: Create proxy environment for Flux instance

=============
flux-proxy(1)
=============


SYNOPSIS
========

**flux** **proxy** [*OPTIONS*] TARGET [command [args...]]

DESCRIPTION
===========

**flux proxy** connects to the Flux instance identified by *TARGET*,
then spawns a shell with FLUX_URI pointing to a local:// socket
managed by the proxy program. As long as the shell is running,
the proxy program routes messages between the instance and the
local:// socket. Once the shell terminates, the proxy program
terminates and removes the socket.

The *TARGET* argument is a URI which can be resolved by ``flux uri``,
including a Flux jobid, a fully-resolved native ``ssh`` or ``local``
URI, or a resolvable URI with a scheme supported by a ``flux uri``
plugin.  See :man1:`flux-uri` for details.

The purpose of **flux proxy** is to allow a connection to be reused,
for example where connection establishment has high latency or
requires authentication.


OPTIONS
=======

**-f, --force**
   Allow the proxy command to connect to a broker running a different
   version of Flux with a warning message instead of a fatal error.

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

Connect to a Flux instance running as job ƒQBfmbm in the current instance:

::

   $ flux proxy ƒQBfmbm

or

::

   $ flux proxy jobid:ƒQBfmbm


Connect to a Flux instance running as job ƒQ8ho35 in ƒQBfmbm

::

  $ flux proxy jobid:ƒQBfmbm/ƒQ8ho35


Connect to a Flux instance started in Slurm job 1234

::

  $ flux proxy slurm:1234


RESOURCES
=========

Flux: http://flux-framework.org
