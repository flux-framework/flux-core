==================
flux-schedbench(1)
==================


SYNOPSIS
========

| **flux** **schedbench** **run** *TEST* [*OPTIONS*]
| **flux** **schedbench** **sweep** [*TEST*] [*OPTIONS*]
| **flux** **schedbench** **report** *TEST* [*OPTIONS*]


DESCRIPTION
===========

.. program:: flux schedbench

:program:`flux schedbench` runs scheduler benchmarks against fake or real
resources and saves the results for later reporting.

By default, :program:`flux schedbench run` launches a fresh Flux subinstance
configured with fake resources via the fake-resources modprobe rc1 task
(see :man5:`flux-config-fake-resources`) and re-execs itself inside that
subinstance to run the benchmark. With ``--exec``, no subinstance is
launched and real jobs run against the broker the user is currently
connected to.

:program:`flux schedbench sweep` runs a cross-product of benchmark
configurations as parallel Flux jobs against the outer instance, sharing
one results file and one live dashboard.  Single-value parameters stay
fixed across the sweep; comma lists and RFC 45 ranges become sweep axes.
Use ``--from FILE.toml`` for structured definitions including multi-module
scheduler recipes.


COMMANDS
========

run
---

.. program:: flux schedbench run

:program:`flux schedbench run` runs a named benchmark (see `BENCHMARKS`_
below) and appends the result to the results file.

.. option:: -N, --nodes=N

   Fake-resource node count for the launched subinstance (default: 4).
   Ignored with :option:`--exec` — the real broker's resources are used
   instead.

.. option:: -c, --cores-per-node=C

   Cores per node in the fake resource set (default: 64). Ignored with
   :option:`--exec`.

.. option:: -g, --gpus-per-node=G

   GPUs per node in the fake resource set (default: 8). Ignored with
   :option:`--exec`.

.. option:: --hwloc-xml-path=PATH

   Path to an hwloc XML file describing per-node topology. Passed to
   the subinstance as ``--conf=fake-resources.hwloc-xml-path=PATH``;
   the XML's per-node shape is replicated across
   :option:`--nodes`. Useful for benchmarking topology-aware schedulers
   (e.g. Fluxion). The path is also recorded in the results file so
   reports can show which run used which topology. Ignored with
   :option:`--exec`.

.. option:: --amend-r=SPEC

   Reference to a Python amender that mutates R before it is written
   to KVS. ``SPEC`` is either a ``module:function`` reference or a
   path to a file with an ``amend()`` callable at module scope; see
   :man5:`flux-config-fake-resources` for the amender contract.
   Passed to the subinstance as
   ``--conf=fake-resources.amend-r=SPEC``. Typically paired with
   :option:`--hwloc-xml-path` to inject scheduler-specific topology
   metadata (Fluxion JGF, node properties, etc.) into R. The spec
   is recorded in the results file. Ignored with :option:`--exec`.

.. option:: --scheduler=NAME

   Scheduler module to load in the fake-resources subinstance (default:
   ``sched-simple``). Implemented by passing
   ``--conf=modules.alternatives.sched=NAME`` to the underlying
   :man1:`flux-start`. Ignored with :option:`--exec`.

.. option:: --scheduler-options=OPTS

   Module options string for the scheduler, shlex-parsed into a list and
   encoded into the subinstance config as
   ``--conf=modules.sched.args=["opt1", "opt2"]``. Ignored with
   :option:`--exec`.

.. option:: -o, --extra-start-options=OPTS

   Extra arguments to pass through to the underlying :man1:`flux-start`
   when launching the fake-resources subinstance; shlex-parsed. May be
   given multiple times. Useful for setting broker attributes
   (``--setattr=...``) or other conf keys that schedbench doesn't have
   a dedicated flag for. Ignored with :option:`--exec`.

.. option:: -x, --exec

   Run real jobs against the current enclosing instance — no fake
   resources, no subinstance launch. Use when running inside an
   allocation where the broker's currently-loaded scheduler and
   currently-up resources are what you want to benchmark.

.. option:: --watcher=NAME

   Event-watcher implementation (default: ``journal``). The journal
   watcher imposes a single subscription on kvs-watch; the per-job watcher
   opens one subscription per job, useful for measuring watcher overhead
   under high job counts. Valid values are ``journal`` and ``per-job``.

.. option:: -n, --njobs=N

   Number of jobs to submit (default: 1000). Used by throughput and
   locality; ignored by fill-machine, which computes its job count
   from the resource shape.

.. option:: --slot-cores=K

   Cores per slot (default: 1). The total cores requested by each
   job is ``--slot-cores * --nslots``.

.. option:: --slot-gpus=K

   GPUs per slot (default: 0). The total GPUs requested by each job
   is ``--slot-gpus * --nslots``.

.. option:: --nslots=N

   Slots per job (locality only; default: 1). Each slot requests
   :option:`--slot-cores` cores and :option:`--slot-gpus` GPUs;
   locality scores how well the scheduler packs each slot into a
   single locality domain.

.. option:: --duration=SPEC

   Job duration (locality only; default: ``0.1s``). ``SPEC`` is one
   of:

   * an FSD string (e.g. ``0.1s``, ``2m``) — every job runs that
     long;
   * ``fill`` — jobs never complete; the benchmark saturates the
     cluster and cancels everything once placement is observed;
   * ``random[:LO-HI]`` — uniform random duration per job (default
     range ``0.1s-1.0s``).

.. option:: --tag=LABEL

   Free-form label stored in the results metadata for filtering and
   reporting.

.. option:: --results-file=PATH

   Results file path (default: ``./schedbench-results.json``). The file
   is created if absent; new runs append to the existing record list.

.. option:: --no-save

   Don't append to the results file. Useful for ad-hoc runs that
   shouldn't pollute the persistent record.

.. option:: --ui=auto|on|off

   Interactive terminal UI: ``auto`` (default) enables it when stdout is a
   TTY and :option:`--quiet` is not set; ``on`` forces it; ``off`` falls
   back to the JSON event stream.

.. option:: --color=auto|always|never

   Color output for the terminal UI: ``auto`` (default) honors NO_COLOR and
   TERM; ``always`` / ``never`` force.

.. option:: -q, --quiet

   Emit only terminal events (``test.start``, ``result``,
   ``test.complete``, ``test.error``). On a non-TTY stdout, produces a
   single JSON object containing the result metrics.

.. option:: -v, --verbose

   Emit progress, info, and metric events from the benchmark.


report
------

.. program:: flux schedbench report

:program:`flux schedbench report` pretty-prints results for a single
benchmark from the results file.

.. option:: --results-file=PATH

   Results file path (default: ``./schedbench-results.json``).

.. option:: --filter=KEY=VAL

   Show only runs matching *KEY=VAL*. May be given multiple times;
   filters combine with AND semantics. Common keys: ``tag``, ``watcher``,
   ``scheduler.name``, ``real_exec``.

.. option:: -o, --format=FORMAT

   Output format. May be a named format (``default``, ``long``, ``csv``,
   or any user-defined format from
   ``~/.config/flux/flux-schedbench-report-TEST.toml``; pass ``help`` to
   list available names), or a literal Python-style format string.

.. option:: --sort=KEY,...

   Sort rows by one or more keys. Prefix a key with ``-`` for descending
   order.

.. option:: --no-header

   Omit the header row.


sweep
-----

.. program:: flux schedbench sweep

:program:`flux schedbench sweep` runs a cross-product of benchmark
configurations and writes all results to a single results file with
shared sweep metadata.  Each point in the matrix becomes one child
:program:`flux schedbench run` invocation submitted as a Flux job to the
outer instance; the dispatcher tracks them in parallel through a single
:class:`flux.job.JobWatcher` and renders progress to a live dashboard.

Sweep flags mirror :program:`flux schedbench run` (same names, same
semantics), with one extension: every axis-capable flag accepts either a
single value (fixed across the sweep) or a multi-value spec that becomes
a sweep axis.  Multi-value specs are:

* a comma list — ``--nodes=4,8,16``
* an RFC 45 range — ``--nodes=4-1024:2:*`` (4 to 1024, step ×2)
* a Python list in TOML — ``njobs = [4096, 8192]``

The axis-capable flags are: :option:`--nodes`, :option:`--cores-per-node`,
:option:`--gpus-per-node`, :option:`--njobs`, :option:`--slot-cores`,
:option:`--slot-gpus`, :option:`--hwloc-xml-path`, :option:`--amend-r`,
:option:`--scheduler`, :option:`--scheduler-options`, :option:`--watcher`,
:option:`--tag`, and the *TEST* positional itself (which accepts a comma
list for a multi-benchmark sweep).

The sweep itself runs against the outer Flux instance.  To run a sweep
without a pre-existing outer instance, wrap in :man1:`flux-start`::

    flux start -s 1 -- flux schedbench sweep ...

Sweep-only flags:

.. option:: --from=PATH

   Load sweep parameters and scheduler recipes from a TOML file.  CLI
   flag values for the same parameters take precedence over file values.
   See `SWEEP TOML FORMAT`_ below.

.. option:: --sweep-name=NAME

   Human-readable label for this sweep, stored in every record's
   ``sweep.name`` field.  Default: a timestamp-based name.

.. option:: --per-run-nodes=N

   Nodes to allocate to each child Flux job (default: 1).  The child
   schedbench process spawns a fake-resources subinstance inside its
   allocation, so for fake-resources benchmarks one outer node per child
   is sufficient regardless of the per-run :option:`--nodes` value.

The standard ``run``-side flags also apply (:option:`--results-file`,
:option:`--no-save`, :option:`--ui`, :option:`--color`, :option:`--conf`,
:option:`--real-exec`).


SWEEP TOML FORMAT
=================

A sweep definition file is a TOML document; the whole file is the sweep
(no outer ``[sweep]`` wrapper).  Top-level scalars become fixed
parameters; top-level lists and RFC 45 strings become sweep axes.
Structured sections (``[[scheduler]]``, ``[conf]``) configure dispatch.

A complete example, using Fluxion from a build directory::

    # Optional sweep label.  Auto-generated if omitted.
    name = "fluxion-vs-simple"

    # Top-level parameters.  Single values are fixed across the
    # sweep; lists and RFC 45 ranges become axes.
    test = "throughput"
    nodes = "16-1024:2"            # axis: 16, 32, 64, ..., 1024
    njobs = [4096, 8192]           # axis: explicit list
    cores_per_node = 64            # fixed: scalar
    real_exec = false              # fixed: scalar

    # Conf entries forwarded to every child run as --conf=KEY=VALUE.
    # Equivalent to passing --conf=KEY=VALUE on the sweep command line.
    [conf]
    "resource.noverify" = true

    # [[scheduler]] array-of-tables: each entry contributes one value
    # to the implicit "scheduler" axis.  ``name`` is the axis label and
    # appears in records / dashboard / report rows.
    [[scheduler]]
    name = "simple"

    [[scheduler]]
    name = "fluxion"
    env = [
      "FLUXION_HOME=/home/user/git/flux-sched",
      "FLUX_MODPROBE_PATH_APPEND=$FLUXION_HOME/etc/modprobe",
      "FLUX_PYTHONPATH_PREPEND=$FLUXION_HOME/src/python",
      "FLUX_MODULE_PATH_PREPEND=$FLUXION_HOME/resource/modules:$FLUXION_HOME/qmanager/modules",
    ]

    [[scheduler.modules]]
    name = "sched-fluxion-resource"
    options = "policy=high"

    [[scheduler.modules]]
    name = "sched-fluxion-qmanager"

Scheduler recipe fields:

``name`` (required)
    Axis value used in records, the dashboard, and report rows.  Also
    the default ``module`` to force-load when no ``modules`` block is
    given (so a shorthand block with just ``name = "sched-backfill"``
    means "force-load sched-backfill").

``module`` (optional)
    Scheduler module name to force-load via
    ``--conf=modules.alternatives.sched=NAME``.  Defaults to ``name``
    in shorthand-form blocks (just ``name``, no ``modules``).  When
    ``modules`` is present, ``module`` defaults to ``None`` — no
    ``--scheduler`` override, modprobe picks the default via priority
    resolution.

``modules`` (optional)
    List of ``{name, options}`` tables.  Each entry's ``options`` (if
    present) is encoded as ``--conf=modules.<name>.args=[...]`` for the
    child run.  Order is informational; modprobe decides load order
    from dependencies.

``conf`` (optional)
    Table of additional ``--conf=KEY=VALUE`` entries applied only when
    this recipe is selected (in addition to the sweep's top-level
    ``[conf]``).  Useful for scheduler-specific tuning.

``env`` (optional)
    List of :class:`flux.job.JobspecV1.from_submit`-style env filter
    rules applied to the child job's environment.  Each rule is one of:

    * ``"VAR=VALUE"`` — set; ``$VAR`` / ``${VAR}`` expand against the
      env built by earlier rules
    * ``"VAR"`` or ``"GLOB*"`` — import from the submitter env
    * ``"-PATTERN"`` — delete matching variables
    * ``"^/path"`` — read additional rules from a file

    The submitter environment is preserved by default; rules only add
    or remove.  As a convenience, a dict ``env = { K = "V", ... }`` is
    accepted and converted to ``["K=V", ...]`` rules — handy for the
    simple "set these vars" case without inter-rule ``$VAR``
    references.

Common TOML pitfall — keys placed after sub-table headers attach to the
*sub-table*, not the parent.  Place top-level keys (``env``, ``conf``)
*before* any ``[[scheduler.modules]]`` headers, since headers terminate
the current scope::

    # CORRECT
    [[scheduler]]
    name = "fluxion"
    env = [...]              # belongs to [[scheduler]]

    [[scheduler.modules]]
    name = "sched-fluxion-resource"

    # WRONG — env attaches to the module entry, not the scheduler
    [[scheduler]]
    name = "fluxion"

    [[scheduler.modules]]
    name = "sched-fluxion-resource"
    env = [...]              # attaches to the module — error

The TOML parser raises a hard error for unknown keys in
``[[scheduler]]`` and ``[[scheduler.modules]]`` blocks to surface this
errors immediately rather than silently dropping the misplaced key.


BENCHMARKS
==========

throughput
   Submit *N* jobs as fast as possible and measure submit, alloc,
   ingest, and clean rates. Use :option:`--njobs` to control the job
   count and :option:`--slot-cores` / :option:`--slot-gpus` to
   control per-job resource requirements. Headline metric:
   ``throughput`` (jobs/sec, broker-side from submit to clean).

fill-machine
   Submit jobs sized to saturate the resource set; measure
   time-to-fill, then cancel and measure cleanup. Use
   :option:`--slot-cores` and :option:`--slot-gpus` to control
   per-job requirements; the job count is computed from the
   resource shape. Headline metric: ``time_to_fill`` (seconds from
   first submit to last ``start`` event).

locality
   Submit multi-slot jobs and score each placement against an hwloc
   topology. A slot is "satisfied" when its cores and GPUs all
   share a single locality domain (by default the NUMA node
   identified by hwloc's ``nodeset`` attribute). Use
   :option:`--nslots`, :option:`--slot-cores`, and
   :option:`--slot-gpus` to shape the request, and
   :option:`--duration` to control job lifetime. Requires
   :option:`--hwloc-xml-path` — the predicate needs the topology to
   score against. Headline metric: ``mean_locality_score`` (mean
   per-job satisfied / requested slots, in ``[0, 1]``).


EXAMPLES
========

Run a basic throughput benchmark against a 4-node fake cluster (the
defaults), saving to the default results file::

   $ flux schedbench run throughput --njobs=500

Benchmark against a larger fake cluster and tag the run::

   $ flux schedbench run throughput -N 100 --cores-per-node=32 \
       --njobs=10000 --tag=large-throughput

Benchmark against an alternative scheduler::

   $ flux schedbench run throughput -N 100 \
       --scheduler=sched-fluxion-qmanager \
       --scheduler-options="queue-depth=64"

Benchmark Fluxion with topology-aware scheduling against a synthetic 100-node
cluster, using the local machine's hwloc topology replicated per node and a
Fluxion JGF amender to populate ``R["scheduling"]``::

   $ lstopo --of xml local.xml
   $ flux schedbench run throughput -N 100 \
       --hwloc-xml-path=./local.xml \
       --amend-r=./fluxion_amender.py \
       --scheduler=sched-fluxion-qmanager

Measure placement quality on a synthetic NUMA-rich topology — how well does
the scheduler keep each 4-core slot inside a single NUMA domain?::

   $ lstopo -i "package:2 numa:4 core:8 pu:1" --of xml > syn.xml
   $ flux schedbench run locality -N 16 --hwloc-xml-path=./syn.xml \
       --nslots=2 --slot-cores=4 --njobs=200

Compare locality and throughput trade-offs across schedulers by running both
benchmarks against each and reading the headline metrics from the same
results file::

   $ for sched in sched-simple sched-fluxion-qmanager; do
   >     flux schedbench run locality -N 16 \
   >         --hwloc-xml-path=./syn.xml --scheduler=$sched \
   >         --tag=$sched --nslots=2 --slot-cores=4 --njobs=200
   > done
   $ flux schedbench report locality --sort=-mean_locality_score

Benchmark inside a real allocation against real hardware::

   $ flux alloc -N 4 -- flux schedbench run throughput --exec --njobs=500

Sweep node counts and schedulers in a single run, with a 1-node child
per point::

   $ flux alloc -N 16 -- flux schedbench sweep throughput \
       --nodes=16,64,256,1024 \
       --scheduler=sched-simple,sched-backfill \
       --njobs=10000

Sweep from a TOML file with a fluxion recipe — multi-module load plus
env overlay::

   $ cat smoke.toml
   test = "fill-machine"
   nodes = "16-1024:2"
   njobs = 49152

   [[scheduler]]
   name = "fluxion"
   env = [
     "FLUXION_HOME=/home/user/git/flux-sched",
     "FLUX_MODPROBE_PATH_APPEND=$FLUXION_HOME/etc/modprobe",
   ]
   [[scheduler.modules]]
   name = "sched-fluxion-resource"
   [[scheduler.modules]]
   name = "sched-fluxion-qmanager"
   $ flux start -s 1 -- flux schedbench sweep --from=smoke.toml

Pretty-print results for one benchmark::

   $ flux schedbench report throughput

Export results to CSV for downstream plotting::

   $ flux schedbench report throughput -o csv > throughput.csv

Show only runs with a particular tag, sorted by throughput descending::

   $ flux schedbench report throughput --filter=tag=large-throughput \
       --sort=-throughput

Show all runs from a single sweep, sorted by node count::

   $ flux schedbench report fill-machine --filter=sweep.name=smoke \
       --sort=nodes


FILES
=====

``./schedbench-results.json``
   Default results file written by :program:`flux schedbench run` and read
   by :program:`flux schedbench report`. Override with
   :option:`--results-file`.

``~/.config/flux/flux-schedbench-report-TEST.toml``
   Per-benchmark user-defined output formats consumed by
   :program:`flux schedbench report -o FORMAT`. See :man5:`flux-config`
   for general format syntax.


ENVIRONMENT
===========

FLUX_SCHEDBENCH_ISOLATED
   Set to ``1`` by :program:`flux schedbench run` when launching the inner
   schedbench invocation inside the fake-resources subinstance. The inner
   invocation checks this variable as a recursion guard and runs the
   benchmark directly rather than launching another subinstance. Users
   should not normally set this.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man5:`flux-config-fake-resources`, :man1:`flux-start`, :man1:`flux-alloc`
