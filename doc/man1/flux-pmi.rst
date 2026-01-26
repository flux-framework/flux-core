===========
flux-pmi(1)
===========


SYNOPSIS
========

| [**launcher**] **flux-pmi** [*-v*] [*--method=URI*] barrier [*--count=N*]
| [**launcher**] **flux-pmi** [*-v*] [*--method=URI*] exchange [*--count=N*]
| [**launcher**] **flux-pmi** [*-v*] [*--method=URI*] get [*--ranks=IDSET*]


DESCRIPTION
===========

.. program:: flux pmi

:program:`flux pmi` is a standalone Process Management Interface (PMI) client
that embeds the same PMI client plugins as :man1:`flux-broker`.  It can be
used to test the PMI service offered by :man1:`flux-shell` to parallel
programs launched by Flux, or to probe the PMI services of an external
launcher like Slurm or Hydra in isolation, without the complications of
starting a Flux instance.

:program:`flux pmi` tries a sequence of PMI plugins until one successfully
initializes.  Alternatively, the specific plugin can be forced with the
:option:`--method` option.  Initialization is followed by a subcommand-specific
sequence of PMI operations that mimics a common pattern, and then PMI
finalization.

OPTIONS
=======

.. option:: -v, --verbose[=LEVEL]

   Trace PMI operations.  This is equivalent to setting
   :envvar:`FLUX_PMI_DEBUG` in the broker environment.

.. option:: --method=URI

   Specify the PMI method to use, where the scheme portion of the URI specifies
   a plugin and the path portion specifies plugin-specific options.  The
   builtin plugins are

   simple
     Use the simple PMI-1 wire protocol.

   libpmi2[:PATH]
     :func:`dlopen` ``libpmi2.so`` and use the PMI-2 API, optionally
     at a specific *PATH*.

   libpmi[:PATH]
     :func:`dlopen` ``libpmi.so`` and use the PMI-1 API, optionally
     at a specific *PATH*.

   single
     Become a singleton.

.. option:: --libpmi-noflux

   Fail if the libpmi or libpmi2 methods find the Flux ``libpmi.so``.

.. option:: --libpmi2-cray

   Force the libpmi2 Cray workarounds to be enabled for testing.  Normally
   they are enabled only if a heuristic detects that Cray libpmi2 is in use.
   The workarounds are

   - Encode all KVS values with base64.
   - Immediately fail an attempt to fetch any KVS with a ``flux.`` prefix.

COMMANDS
========

barrier
-------

.. program:: flux pmi barrier

:program:`flux pmi barrier` executes a PMI barrier and prints a message
on rank 0 upon completion.

.. option:: --test-timing

  First execute a preliminary barrier to ensure all ranks enter the test
  barrier at the same time, then execute a test barrier and print the elapsed
  time for the test barrier on rank 0 stdout.

.. option:: --test-count=N

  Execute N barrier operations (default 1).

.. option:: --test-abort=RANK

  Instead of entering the barrier, arrange for RANK to call the PMI
  abort function.  This option is intended for testing only.

exchange
--------

.. program:: flux pmi exchange

:program:`flux pmi exchange` does the following:

  #. Execute PMI barrier
  #. Put rank specific key to PMI KVS
  #. Execute PMI barrier
  #. Get rank specific key from PMI KVS for all other ranks
  #. Execute PMI barrier
  #. Print elapsed time of (2-5)

.. option:: --count=N

  Execute N exchange (step 2-5) operations (default 1).

get
---

.. program:: flux pmi get

:program:`flux pmi get` fetches a pre-set key from the PMI KVS.

.. option:: --ranks=RANKS

   Print the value on specified *RANKS*, an RFC 22 idset or ``all`` (default 0).


RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_13`


SEE ALSO
========

:man7:`flux-broker-attributes`
