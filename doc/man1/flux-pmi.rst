===========
flux-pmi(1)
===========


SYNOPSIS
========

| [**launcher**] **flux-pmi** [*-v*] [*--method=URI*] barrier [*--count=N*] [*--group=GROUP*]
| [**launcher**] **flux-pmi** [*-v*] [*--method=URI*] exchange [*--count=N*] [*--group=GROUP*]
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

.. option:: -t, --timeout=FSD

   Specify a timeout for the requested PMI operation(s) in RFC 23 Flux
   Standard Duration format, e.g. "0.1s" or "32m".
   If the timeout expires, the command exits with an error code.
   Default: no timeout.

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

.. option:: --group={world|node|self|IDSET}

  Execute a PMI-1.2 group barrier instead of a regular barrier.  *world*
  spans all ranks, *node* spans the ranks co-located on this node, *self*
  completes locally, and an RFC 22 *IDSET* names an explicit set of ranks.
  With an explicit set, ranks that are not members skip the barrier.  The
  completion message is printed by the lowest member rank.  This fails if
  the PMI method or server does not support PMI-1.2 group barriers.

.. option:: --tag=STRING

  Set the string tag that distinguishes concurrent group barriers.
  Only meaningful with :option:`--group`.

.. option:: --test-timing

  First execute a preliminary barrier to ensure all ranks enter the test
  barrier at the same time, then execute a test barrier and print the elapsed
  time for the test barrier on the lowest member rank.

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

.. option:: --group={world|self|IDSET}

  Restrict the exchange to a PMI-1.2 group, using a group barrier in
  step 3 and getting keys only from the group members in step 4.
  Non-member ranks skip steps 2-4 but still execute the barriers in
  steps 1 and 5.  The completion message is printed by the lowest member
  rank.  The *node* group is not supported here, since the client cannot
  enumerate the node's members to verify their keys.

.. option:: --tag=STRING

  Set the group barrier string tag.  Only meaningful with
  :option:`--group`.

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
