=============
flux-sproc(1)
=============


SYNOPSIS
========

**flux** **sproc** *ps* [*-n*] [*--rank=RANK*] [*--service=SERVICE*] [*--format=FORMAT*]
**flux** **sproc** *kill* [*-w*] [*--rank=RANK*] [*--service=SERVICE*] *SIGNUM* *PID|LABEL*
**flux** **sproc** *wait* [*--rank=RANK*] [*--service=SERVICE*] *PID|LABEL*


DESCRIPTION
===========

:program:`flux sproc` manages subprocesses running under Flux subprocess
servers such as **rexec** and **sdexec**. It provides commands to list,
signal, and wait for remote subprocesses.

Subprocesses can be identified by either process ID (pid) or by label if
one was set with :man1:`flux-exec`.

COMMANDS
========

ps
--

.. program:: flux sproc ps

List active and zombie subprocesses.

.. option:: -r, --rank=RANK

   Send RPC to specified broker rank. The default is FLUX_NODEID_ANY.

.. option:: -s, --service=NAME

   Query the specified service. The default is **rexec**.

.. option:: -o, --format=FORMAT

   Specify output format using Python's string format syntax. Supported
   field names include **pid**, **state**, **label**, **rank**, and **cmd**.
   The default format is ``{pid:>9} {state:<2} {label:<12} {cmd}``.

.. option:: -n, --no-header

   Suppress printing of header line.

The **state** field shows the subprocess state: **R** for running or **Z**
for zombie (exited but not yet waited on).


kill
----

.. program:: flux sproc kill

Send a signal to a subprocess.

.. option:: -r, --rank=RANK

   Send RPC to specified broker rank. The default is FLUX_NODEID_ANY.

.. option:: -s, --service=NAME

   Send signal via the specified service. The default is **rexec**.

.. option:: -w, --wait

   Wait for the process to exit and return its exit status. If the process
   has already exited before :command:`flux sproc kill` is called and the
   process is waitable, then an error will be emitted because the kill
   request will fail, but the command will still exit with the collected
   exit status from a successful wait.

.. option:: SIGNUM

   The signal number to send (e.g., 15 for SIGTERM, 9 for SIGKILL).

.. option:: PID|LABEL

   Process identifier or label. May be a numeric pid or a string label
   that was set with the ``--label`` option of :man1:`flux-exec`.


wait
----

.. program:: flux sproc wait

Wait for a waitable subprocess to complete and return its exit status.

.. note::
   This command only works on subprocesses started with the waitable flag,
   such as those created with :command:`flux-exec --bg --waitable`. Attempting
   to wait on a non-waitable process will fail with an error.

.. option:: -r, --rank=RANK

   Send RPC to specified broker rank. The default is FLUX_NODEID_ANY.

.. option:: -s, --service=NAME

   Wait via the specified service. The default is **rexec**.

.. option:: PID|LABEL

   Process identifier or label. May be a numeric pid or a string label
   that was set with the ``--label`` option of :man1:`flux-exec`.

The command exits with the subprocess exit code, or 128 + signal number if
the subprocess was terminated by a signal.


EXAMPLES
========

List all subprocesses on rank 0::

   $ flux sproc ps -r 0

Kill a subprocess by pid::

   $ flux sproc kill 15 12345

Kill a subprocess by label::

   $ flux sproc kill 9 my-process

Wait for a waitable subprocess::

   $ flux sproc wait my-background-job
   $ echo $?
   0

Custom output format::

   $ flux sproc ps -n -o '{pid}'


RESOURCES
=========

.. include:: common/resources.rst

:doc:`rfc:spec_42`

SEE ALSO
========

:man1:`flux-exec`
