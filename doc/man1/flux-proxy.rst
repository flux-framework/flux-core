.. flux-help-command: proxy
.. flux-help-description: proxy connections to Flux jobs and instances
.. flux-help-section: jobs

=============
flux-proxy(1)
=============


SYNOPSIS
========

**flux** **proxy** [*OPTIONS*] TARGET [command [args...]]

DESCRIPTION
===========

.. program:: flux proxy

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

If the connection to the Flux instance is lost, for example when the
target instance terminates, **flux proxy** will emit an error message,
send ``SIGHUP`` and ``SIGCONT`` to the spawned shell or other process,
and wait for it to terminate before exiting.  The delivery of signals
can be disabled with the ``-n, --nohup`` option, but be aware that Flux
commands running under a **flux proxy** which has lost its connection
will likely result in errors.

The purpose of **flux proxy** is to allow a connection to be reused,
for example where connection establishment has high latency or
requires authentication.


OPTIONS
=======

.. option:: -f, --force

   Allow the proxy command to connect to a broker running a different
   version of Flux with a warning message instead of a fatal error.

.. option:: -n, --nohup

   When an error occurs in the proxy connection, **flux proxy** will
   normally shut down the proxy and send ``SIGHUP`` and ``SIGCONT`` to
   the spawned shell or command. If the ``-n, --nohup`` option is used,
   the ``SIGHUP`` and ``SIGCONT`` signals will not be sent.
   **flux proxy** will still wait for the spawned shell or command to
   exit before terminating to avoid having the child process reparented
   and possibly lose its controlling tty.

.. option:: --reconnect

   If broker communication fails, drop the current connection and try to
   reconnect every 2 seconds until the connection succeeds.  Any event
   subscriptions and service registrations that were made on behalf of
   clients are re-established, and in-flight RPCs receive an ECONNRESET
   error responses.


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
