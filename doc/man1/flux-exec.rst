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
:man1:`flux-shell` processes.  Normally there is only one broker process per
node, and one job shell per broker, meaning that one copy of *COMMAND* is
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

   Label lines of output with the source broker RANK.  This option is not
   affected by :option:`--jobid`.

.. option:: -n, --noinput

   Do not attempt to forward stdin. Send EOF to remote process stdin.

.. option:: -d, --dir=DIR

   Set the working directory of remote *COMMAND* to *DIR*. The default is to
   propagate the current working directory of flux-exec(1).

.. option:: -r, --rank=IDSET

   Target specific ranks, where *IDSET* is a set of zero-origin node ranks in
   RFC 22 format.  If :option:`--jobid` is specified, the ranks are interpreted
   as an index into the list of nodes assigned to the job.  Otherwise, they
   refer to the nodes assigned to the Flux instance.

   The default is to target all ranks.  As a special case, :option:`--rank=all`
   is accepted and behaves the same as the default.

.. option:: -x, --exclude=IDSET

   Exclude specific ranks.  *IDSET* is as described in :option:`--rank`.

.. option:: -j, --jobid=JOBID

   Run *COMMAND* on the nodes allocated to *JOBID* instead of the nodes
   assigned to the Flux instance.

   This uses the exec service embedded in :man1:`flux-shell` rather than
   :man1:`flux-broker`.

   The interpretation of :option:`--rank` and :option:`--exclude` is adjusted
   as noted in their descriptions.  For example, :option:`flux exec -j ID -r 0`
   will run only on the first node assigned to *JOBID*, and
   :option:`flux exec -j ID -x 0` will run on all nodes assigned to *JOBID*
   except the first node.

   This option is only available when the job owner is the same as the Flux
   instance owner.

.. option:: -v, --verbose

   Run with more verbosity.

.. option:: -q, --quiet

   Suppress extraneous output (e.g. per-rank error exit status).

.. option:: --with-imp

   Prepend the full path to :program:`flux-imp run` to *COMMAND*. This option
   is mostly meant for testing or as a convenience to execute a configured
   ``prolog`` or ``epilog`` command under the IMP.

.. option:: --bg

   Run processes in background. The rank and PID of each successfully
   started process are written to stdout. The program exits once all
   requested processes have started or failed, but processes continue
   running as children of the server until they exit or the server shuts
   down. Process output is logged to the server's stdout and stderr. The
   :option:`--label-io` and :option:`--no-input` options are ignored.

.. option:: --waitable

   Make the subprocess waitable. This option is only allowed when combined
   with :option:`--bg`.

   When a background subprocess is marked waitable, it remains in the
   subprocess server's process list after it exits, entering a zombie state
   until its exit status is collected via a wait RPC. Without this flag,
   background subprocesses are automatically reaped when they exit and their
   status cannot be retrieved.

.. option:: --label=LABEL

   Set a string label for the remote process. When specified, the label
   allows the process to be referenced by name in addition to process ID
   in subsequent operations. The label appears in process list responses
   and must be unique across all processes managed by the subprocess server.

CAVEATS
=======

In a multi-user flux instance, access to the rank 0 broker execution
service is restricted to requests that originate from the local broker.
Therefore, :program:`flux exec` (without :option:`--jobid`) must be run
from the rank 0 broker if rank 0 is included in the target *IDSET*.

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
