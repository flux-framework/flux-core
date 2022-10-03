.. flux-help-include: true

=============
flux-start(1)
=============


SYNOPSIS
========

**flux** **start** [*OPTIONS*] [initial-program [args...]]

DESCRIPTION
===========

flux-start(1) launches a new Flux instance. By default, flux-start
execs a single :man1:`flux-broker` directly, which will attempt to use
PMI to fetch job information and bootstrap a flux instance.

If a size is specified via *--test-size*, an instance of that size is to be
started on the local host with flux-start as the parent.

A failure of the initial program (such as non-zero exit code)
causes flux-start to exit with a non-zero exit code.


OPTIONS
=======

**-o, --broker-opts**\ =\ *option_string*
   Add options to the message broker daemon, separated by commas.

**-v, --verbose**\ =\ *[LEVEL]*
   This option may be specified multiple times, or with a value, to
   set a verbosity level.  See VERBOSITY LEVELS below.

**-X, --noexec**
   Don't execute anything. This option is most useful with -v.

**--caliper-profile**\ =\ *PROFILE*
   Run brokers with Caliper profiling enabled, using a Caliper
   configuration profile named *PROFILE*. Requires a version of Flux
   built with --enable-caliper. Unless CALI_LOG_VERBOSITY is already
   set in the environment, it will default to 0 for all brokers.

**--rundir**\ =\ *DIR*
   (only with *--test-size*) Set the directory that will be
   used as the rundir directory for the instance. If the directory
   does not exist then it will be created during instance startup.
   If a DIR is not set with this option, a unique temporary directory
   will be created. Unless DIR was pre-existing, it will be removed
   when the instance is destroyed.

**--wrap**\ =\ *ARGS,…​*
   Wrap broker execution in a comma-separated list of arguments. This is
   useful for running flux-broker directly under debuggers or valgrind.

**-s, --test-size**\ =\ *N*
   Launch an instance of size *N* on the local host.

**--test-hosts**\ =\ *HOSTLIST*
   Set FLUX_FAKE_HOSTNAME in the environment of each broker so that the
   broker can bootstrap from a config file instead of PMI.  HOSTLIST is
   assumed to be in rank order.  The broker will use the fake hostname to
   find its entry in the configured bootstrap host array.

**--test-exit-timeout**\ =\ *FSD*
   After a broker exits, kill the other brokers after a timeout (default 20s).

**--test-exit-mode**\ =\ *MODE*
   Set the mode for the exit timeout.  If set to ``leader``, the exit timeout
   is only triggered upon exit of the leader broker, and the flux-start exit
   code is that of the leader broker.  If set to ``any``, the exit timeout
   is triggered upon exit of any broker, and the flux-start exit code is the
   highest exit code of all brokers.  Default: ``any``.

**--test-start-mode**\ =\ *MODE*
   Set the start mode.  If set to ``all``, all brokers are started immediately.
   If set to ``leader``, only the leader is started.  Hint: in ``leader`` mode,
   use ``--setattr=broker.quorum=0`` to let the initial program start before
   the other brokers are online.  Default: ``all``.

**--test-rundir**\ =\ *PATH*
   Set the directory to be used as the broker rundir instead of creating a
   temporary one.  The directory must exist, and is not cleaned up unless
   ``--test-rundir-cleanup`` is also specified.

**--test-rundir-cleanup**
   Recursively remove the directory specified with ``--test-rundir`` upon
   completion of flux-start.

**--test-pmi-clique**\ =\ *MODE*
   Set the pmi clique mode, which determines how ``PMI_process_mapping`` is set
   in the PMI server used to bootstrap the brokers.  If ``none``, the mapping
   is not created.  If ``single``, all brokers are placed in one clique. If
   ``per-broker``, each broker is placed in its own clique.
   Default: ``single``.

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


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man1:`flux-broker`
