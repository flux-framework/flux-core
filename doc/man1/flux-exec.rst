============
flux-exec(1)
============


SYNOPSIS
--------
**flux** **exec** [--noinput] [*--label-io] [*—dir=DIR'] [*--rank=NODESET*] [*--verbose*] COMMANDS...

DESCRIPTION
===========

.. program:: flux exec

:program:`flux exec` runs commands across one or more Flux broker ranks using
the *broker.exec* service. The commands are executed as direct children
of the broker, and the broker handles buffering stdout and stderr and
sends the output back to :program:`flux exec` which copies output to its own
stdout and stderr.

On receipt of SIGINT and SIGTERM signals, :program:`flux exec` shall forward
the received signal to all currently running remote processes.

In the event subprocesses are hanging or ignoring SIGINT, two SIGINT
signals (typically sent via Ctrl+C) in short succession can force
:program:`flux exec` to exit.

:program:`flux exec` is meant as an administrative and test utility, and cannot
be used to launch Flux jobs.


EXIT STATUS
===========

In the case that all processes are successfully launched, the exit status
of :program:`flux exec` is the largest of the remote process exit codes.

If a non-existent rank is targeted, :program:`flux exec` will return with
code 68 (EX_NOHOST from sysexits.h).

If one or more remote commands are terminated by a signal, then
:program:`flux exec` exits with exit code 128+signo.


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
   See `NODESET FORMAT`_ below for more information.

.. option:: -x, --exclude=NODESET

   Exclude ranks in *NODESET*.

.. option:: -j, --jobid=JOBID

   Use the job shell exec service for job *JOBID* instead of the broker's
   exec service. The default *NODESET* will be set to the nodes assigned
   to the job, and the :option:`--rank` and :option:`--exclude` options are
   applied relative to this set. For example, :option:`flux exec -j ID -r 0`
   will run only on the first node assigned to *JOBID*, and :option:`flux
   exec -j ID -x 0` will run on all nodes assigned to *JOBID* except the
   first node.

.. option:: -v, --verbose

   Run with more verbosity.

.. option:: -q, --quiet

   Suppress extraneous output (e.g. per-rank error exit status).

.. option:: --with-imp

   Prepend the full path to :program:`flux-imp run` to *COMMANDS*. This option
   is mostly meant for testing or as a convenience to execute a configured
   ``prolog`` or ``epilog`` command under the IMP. Note: When this option is
   used, or if :program:`flux-imp` is detected as the first argument of
   *COMMANDS*, :program:`flux exec` will use :program:`flux-imp kill` to
   signal remote commands instead of the normal builtin subprocess signaling
   mechanism.


NODESET FORMAT
==============

.. include:: common/nodeset.rst


RESOURCES
=========

.. include:: common/resources.rst
