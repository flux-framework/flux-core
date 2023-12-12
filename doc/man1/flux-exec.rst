============
flux-exec(1)
============

SYNOPSIS
========

**flux** **exec** [*--noinput*] [*--label-io*] [*—dir=DIR*] [*--rank=IDSET*] [*--verbose*] *COMMAND...*

DESCRIPTION
===========

.. program:: flux exec

:program:`flux exec` remotely executes one or more copies of *COMMAND*,
similar to :linux:man1:`pdsh`.  It bypasses the scheduler and is intended
for launching administrative commands or tool daemons, not for launching
parallel jobs.  For that, see :man1:`flux-run`.

By default, *COMMAND* runs across all :man1:`flux-broker` processes.  If the
:option:`--jobid` option is specified, the commands are run across a job's
:man1:`flux-shell` processes.  Normally this means that one copy of *COMMAND*
is executed per node, but in unusual cases it could mean more (e.g. if the
Flux instance was started with multiple brokers per node).

Standard output and standard error of the remote commands are captured
and combined on the :program:`flux exec` standard output and standard error.
Standard input of :program:`flux exec` is captured and broadcast to standard
input of the remote commands.

On receipt of SIGINT and SIGTERM signals, :program:`flux exec` forwards
the received signal to the remote processes.  When standard input of
:program:`flux exec` is a terminal, :kbd:`Control-C` may be used to send
SIGINT.  Two of those in short succession can force :program:`flux exec`
to exit in the event that remote processes are hanging.

OPTIONS
=======

.. option:: -l, --label-io

   Label lines of output with the source RANK.

.. option:: -n, --noinput

   Do not attempt to forward stdin. Send EOF to remote process stdin.

.. option:: -d, --dir=DIR

   Set the working directory of remote *COMMAND* to *DIR*. The default is to
   propagate the current working directory of flux-exec(1).

.. option:: -r, --rank=IDSET

   Target specific ranks in *IDSET*. Default is to target "all" ranks.

.. option:: -x, --exclude=IDSET

   Exclude ranks in *IDSET*.

.. option:: -j, --jobid=JOBID

   Use the job shell exec service for job *JOBID* instead of the broker's
   exec service. The default *IDSET* will be set to the nodes assigned
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

   Prepend the full path to :program:`flux-imp run` to *COMMAND*. This option
   is mostly meant for testing or as a convenience to execute a configured
   ``prolog`` or ``epilog`` command under the IMP. Note: When this option is
   used, or if :program:`flux-imp` is detected as the first argument of
   *COMMAND*, :program:`flux exec` will use :program:`flux-imp kill` to
   signal remote commands instead of the normal builtin subprocess signaling
   mechanism.

EXIT STATUS
===========

In the case that all processes are successfully launched, the exit status
of :program:`flux exec` is the largest of the remote process exit codes.

If a non-existent rank is targeted, :program:`flux exec` will return with
code 68 (EX_NOHOST from sysexits.h).

If one or more remote commands are terminated by a signal, then
:program:`flux exec` exits with exit code 128+signo.

RESOURCES
=========

.. include:: common/resources.rst

FLUX RFC
========

:doc:`rfc:spec_22`
