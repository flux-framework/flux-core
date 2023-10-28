============
flux-exec(1)
============


SYNOPSIS
--------
**flux** **exec** [--noinput] [*--label-io] [*—dir=DIR'] [*--rank=NODESET*] [*--verbose*] COMMANDS...

DESCRIPTION
===========

.. program:: flux exec

flux-exec(1) runs commands across one or more flux-broker ranks using
the *broker.exec* service. The commands are executed as direct children
of the broker, and the broker handles buffering stdout and stderr and
sends the output back to flux-exec(1) which copies output to its own
stdout and stderr.

On receipt of SIGINT and SIGTERM signals, flux-exec(1) shall forward
the received signal to all currently running remote processes.

In the event subprocesses are hanging or ignoring SIGINT, two SIGINT
signals (typically sent via Ctrl+C) in short succession can force
flux-exec(1) to exit.

flux-exec(1) is meant as an administrative and test utility, and cannot
be used to launch Flux jobs.


EXIT STATUS
===========

In the case that all processes are successfully launched, the exit status
of flux-exec(1) is the largest of the remote process exit codes.

If a non-existent rank is targeted, flux-exec(1) will return with
code 68 (EX_NOHOST from sysexits.h).

If one or more remote commands are terminated by a signal, then flux-exec(1)
exits with exit code 128+signo.


OPTIONS
=======

.. option:: -l, --label-io

   Label lines of output with the source RANK.

.. option:: -n, --noinput

   Do not attempt to forward stdin. Send EOF to remote process stdin.

.. option:: -d, --dir=DIR

   Set the working directory of remote *COMMANDS* to *DIR*. The default is to
   propagate the current working directory of flux-exec(1).

.. option:: -r, --rank=NODESET

   Target specific ranks in *NODESET*. Default is to target "all" ranks.
   See NODESET FORMAT below for more information.

.. option:: -v, --verbose

   Run with more verbosity.

.. option:: -q, --quiet

   Suppress extraneous output (e.g. per-rank error exit status).

.. option:: --with-imp

   Prepend ``/path/to/flux-imp run`` to *COMMANDS*. This option is mostly
   meant for testing or as a convenience to execute a configured ``prolog``
   or ``epilog`` command under the IMP. Note: When this option is used,
   or if ``flux-imp`` is detected as the first argument of *COMMANDS*,
   flux-exec(1) will use ``flux-imp kill`` to signal remote commands
   instead of the normal builtin subprocess signaling mechanism.


NODESET FORMAT
==============

.. include:: common/nodeset.rst


RESOURCES
=========

Flux: http://flux-framework.org
