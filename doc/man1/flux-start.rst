.. flux-help-include: true

=============
flux-start(1)
=============


SYNOPSIS
========

**flux** **start** [*OPTIONS*] [initial-program [args...]]

DESCRIPTION
===========

.. program:: flux start

:program:`flux start` launches a new Flux instance. By default,
:program:`flux start` execs a single :man1:`flux-broker` directly, which
will attempt to use PMI to fetch job information and bootstrap a flux instance.

If a size is specified via :option:`--test-size`, an instance of that size is
to be started on the local host with :program:`flux start` as the parent.

A failure of the initial program (such as non-zero exit code)
causes :program:`flux start` to exit with a non-zero exit code.


OPTIONS
=======

.. option:: -o, --broker-opts=OPTIONS

   Add options to the message broker daemon, separated by commas.

.. option:: -v, --verbose=[LEVEL]

   This option may be specified multiple times, or with a value, to
   set a verbosity level.  See `VERBOSITY LEVELS`_ below.

.. option:: -X, --noexec

   Don't execute anything. This option is most useful with -v.

.. option:: --caliper-profile=PROFILE

   Run brokers with Caliper profiling enabled, using a Caliper
   configuration profile named *PROFILE*. Requires a version of Flux
   built with :option:`--enable-caliper`. Unless :envvar:`CALI_LOG_VERBOSITY`
   is already set in the environment, it will default to 0 for all brokers.

.. option:: --rundir=DIR

   (only with :option:`--test-size`) Set the directory that will be
   used as the rundir directory for the instance. If the directory
   does not exist then it will be created during instance startup.
   If a DIR is not set with this option, a unique temporary directory
   will be created. Unless DIR was pre-existing, it will be removed
   when the instance is destroyed.

.. option:: --wrap=ARGS

   Wrap broker execution in a comma-separated list of arguments. This is
   useful for running flux-broker directly under debuggers or valgrind.

.. option:: -s, --test-size=N

   Launch an instance of size *N* on the local host.

.. option:: --test-hosts=HOSTLIST

   Set :envvar:`FLUX_FAKE_HOSTNAME` in the environment of each broker so that
   the broker can bootstrap from a config file instead of PMI.  HOSTLIST is
   assumed to be in rank order.  The broker will use the fake hostname to
   find its entry in the configured bootstrap host array.

.. option:: --test-exit-timeout=FSD

   After a broker exits, kill the other brokers after a timeout (default 20s).

.. option:: --test-exit-mode=MODE

   Set the mode for the exit timeout.  If set to ``leader``, the exit timeout
   is only triggered upon exit of the leader broker, and the
   :program:`flux start` exit code is that of the leader broker.  If set to
   ``any``, the exit timeout is triggered upon exit of any broker, and the
   :program:`flux start` exit code is the highest exit code of all brokers.
   Default: ``any``.

.. option:: --test-start-mode=MODE

   Set the start mode.  If set to ``all``, all brokers are started immediately.
   If set to ``leader``, only the leader is started.  Hint: in ``leader`` mode,
   use :option:`--setattr=broker.quorum=1` to let the initial program start
   before the other brokers are online.  Default: ``all``.

.. option:: --test-rundir=PATH

   Set the directory to be used as the broker rundir instead of creating a
   temporary one.  The directory must exist, and is not cleaned up unless
   :option:`--test-rundir-cleanup` is also specified.

.. option:: --test-rundir-cleanup

   Recursively remove the directory specified with :option:`--test-rundir` upon
   completion of :program:`flux start`.

.. option:: --test-pmi-clique=MODE

   Set the pmi clique mode, which determines how ``PMI_process_mapping`` is set
   in the PMI server used to bootstrap the brokers.  If ``none``, the mapping
   is not created.  If ``single``, all brokers are placed in one clique. If
   ``per-broker``, each broker is placed in its own clique.
   Default: ``single``.

.. option:: -r, --recovery=[TARGET]

   Start the rank 0 broker of an instance in recovery mode.  If *TARGET*
   is a directory, treat it as a *statedir* from a previous instance.
   If *TARGET* is a file, treat it as an archive file from :man1:`flux-dump`.
   If *TARGET* is unspecified, assume the system instance is to be recovered.
   In recovery mode, any rc1 errors are ignored, broker peers are not allowed
   to connect, and resources are offline.

.. option:: --sysconfig

   Run the broker with :option:`--config-path` set to the default system
   instance configuration directory.  This option is unnecessary if
   :option:`--recovery` is specified without its optional argument.  It may
   be required if recovering a dump from a system instance.

VERBOSITY LEVELS
================

level 1 and above
   Display commands before executing them.

level 2 and above
   Trace PMI server requests (test mode only).


EXAMPLES
========

Launch an 8-way local Flux instance with an interactive shell as the
initial program and all logs output to stderr:

::

   flux start -s8 -o,--setattr=log-stderr-level=7

Launch an 8-way Flux instance within a slurm job, with an interactive
shell as the initial program:

::

   srun --pty -N8 flux start

Start the system instance rank 0 broker in recovery mode:

::

   sudo -u flux flux start --recovery

Start a non-system instance in recovery mode:

::

   flux start --recovery=/tmp/statedir


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man1:`flux-broker`
