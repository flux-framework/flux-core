###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""flux schedbench: run scheduler benchmarks, save results, report.

By default, ``flux schedbench run`` launches a fresh Flux subinstance
configured with fake resources (via the fake-resources modprobe rc1 task)
and re-execs itself inside that subinstance to run the benchmark. With
``--exec``, no subinstance is launched and real jobs run against the
broker the user is currently connected to.

Subcommands: run     Run a named benchmark and append the result to the
results file. report  Pretty-print the results file as a table.
"""

import argparse
import getpass
import json
import logging
import os
import shlex
import socket
import sys
import time

import flux
import flux.resource
from flux.modprobe import ModuleList
from flux.testing.events import (
    NORMAL,
    QUIET,
    VERBOSE,
    TestEventEmitter,
)
from flux.testing.job_watcher import (
    JournalEventWatcher,
    PerJobEventWatcher,
)
from flux.testing.schedbench import BENCHMARKS, Benchmark, BenchmarkResults
from flux.testing.schedbench.ui import TerminalEmitter
from flux.util import CLIMain, OutputFormat, UtilConfig, get_treedict, help_formatter

LOGGER = logging.getLogger("flux-schedbench")
SCHEDBENCH_VERSION = "0.1.0"

#: Sentinel env var set on the outer schedbench process when it re-execs
#: itself inside a fake-resources subinstance via ``flux start``. The inner
#: invocation checks for this to short-circuit recursive launching and run
#: the benchmark in the broker it's already attached to (see :func:`cmd_run`).
_ISOLATED_ENV_VAR = "FLUX_SCHEDBENCH_ISOLATED"

#: Maps --watcher NAME to a factory taking a Flux handle. The factory is
#: passed to BulkRun via the benchmark constructor; omitting it (None) keeps
#: BulkRun's default (journal).
_WATCHERS = {
    "journal": JournalEventWatcher,
    "per-job": PerJobEventWatcher,
}


def _add_common_run_opts(parser):
    """Add common --opts for 'flux schedbench run' to the parser."""
    parser.add_argument(
        "-N",
        "--nodes",
        type=int,
        default=4,
        metavar="N",
        help="fake-resource node count for the launched "
        "subinstance (default: 4; ignored with --exec — the "
        "real broker's resources are used instead)",
    )
    parser.add_argument(
        "-c",
        "--cores-per-node",
        type=int,
        default=64,
        metavar="C",
        help="cores per node (default: 64; ignored with --exec)",
    )
    parser.add_argument(
        "-g",
        "--gpus-per-node",
        type=int,
        default=8,
        metavar="G",
        help="GPUs per node (default: 8; ignored with --exec)",
    )
    parser.add_argument(
        "-n",
        "--njobs",
        type=int,
        default=1000,
        metavar="N",
        help="number of jobs to submit (default: 1000). Each "
        "benchmark uses this differently: throughput and locality "
        "submit exactly this many; fill-machine derives its job "
        "count from the resource set and ignores this value; "
        "locality's --duration=fill mode likewise overrides.",
    )
    parser.add_argument(
        "--slot-cores",
        type=int,
        default=1,
        metavar="K",
        help="cores per slot (default: 1). Per-task core "
        "requirement for jobs submitted by this benchmark.",
    )
    parser.add_argument(
        "--slot-gpus",
        type=int,
        default=0,
        metavar="K",
        help="GPUs per slot (default: 0). Per-task GPU requirement "
        "for jobs submitted by this benchmark.",
    )
    parser.add_argument(
        "--hwloc-xml-path",
        metavar="PATH",
        help="path to an hwloc XML file describing per-node "
        "topology; passed to the subinstance as "
        "--conf=fake-resources.hwloc-xml-path=PATH. The XML's "
        "per-node shape is replicated across --nodes. Useful for "
        "testing topology-aware schedulers (e.g. Fluxion). "
        "Ignored with --exec.",
    )
    parser.add_argument(
        "--amend-r",
        metavar="SPEC",
        help="reference to a Python amender that mutates R before "
        "it is written to KVS, in either ``module:function`` form "
        "or as a path to a file with an ``amend()`` callable "
        "(see flux-config-fake-resources(5) AMENDERS section). "
        "Passed to the subinstance as "
        "--conf=fake-resources.amend-r=SPEC. Typically paired with "
        "--hwloc-xml-path to inject scheduler-specific topology "
        "metadata into R. Ignored with --exec.",
    )
    parser.add_argument(
        "--scheduler",
        default="sched-simple",
        metavar="NAME",
        help="scheduler module to load in the fake-resources "
        "subinstance via --conf=modules.alternatives.sched "
        "(default: sched-simple). Ignored with --exec — in "
        "--exec mode, the broker's currently-loaded scheduler "
        "is used as-is. In both modes, the recorded scheduler "
        "name is read from the broker post-setup via the "
        "sched-service lookup — so the results record reflects "
        "what was actually loaded, not what was requested.",
    )
    parser.add_argument(
        "--scheduler-options",
        metavar="OPTS",
        help="module options string for the scheduler; "
        "shlex-parsed into a list and encoded into the "
        "subinstance config as "
        '--conf=modules.sched.args=["opt1", "opt2"]. '
        "Ignored with --exec.",
    )
    parser.add_argument(
        "--conf",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="extra config setting forwarded to the underlying "
        "`flux start` as --conf=KEY=VALUE. May be given multiple "
        "times. Convenience for the common case of "
        "-o '--conf=KEY=VALUE'; equivalent to that form but "
        "shorter, and accumulates across multiple uses. Ignored "
        "with --exec.",
    )
    parser.add_argument(
        "-o",
        "--extra-start-options",
        action="append",
        default=[],
        metavar="OPTS",
        help="extra arguments to pass through to the underlying "
        "`flux start` when launching the fake-resources "
        "subinstance; shlex-parsed. May be given multiple "
        "times. Useful for broker attributes (--setattr=...) "
        "and other non-config flags; for plain --conf=KEY=VALUE "
        "settings prefer the dedicated --conf option above. "
        "Ignored with --exec.",
    )
    parser.add_argument(
        "-x",
        "--exec",
        dest="real_exec",
        action="store_true",
        help="run real jobs against the current enclosing "
        "instance (no fake resources, no subinstance launch). "
        "Use when running inside an allocation where the "
        "broker's currently-loaded scheduler and currently-up "
        "resources are what you want to benchmark.",
    )
    parser.add_argument(
        "--watcher",
        choices=sorted(_WATCHERS.keys()),
        default="journal",
        help="event-watcher implementation (default: journal). "
        "The journal watcher imposes a single subscription on "
        "kvs-watch; the per-job watcher opens one subscription "
        "per job, useful for measuring watcher overhead under "
        "high job counts.",
    )
    parser.add_argument(
        "--tag",
        metavar="LABEL",
        default="",
        help="free-form label stored in results metadata",
    )
    parser.add_argument(
        "--results-file",
        default="./schedbench-results.json",
        metavar="PATH",
        help="results file path (default: ./schedbench-results.json)",
    )
    parser.add_argument(
        "--no-save",
        action="store_true",
        help="don't append to the results file",
    )
    parser.add_argument(
        "-q",
        "--quiet",
        action="store_true",
        help="emit only terminal events (test.start, result, "
        "test.complete, test.error); forces JSON output",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="emit progress/info/metric events",
    )
    parser.add_argument(
        "--ui",
        choices=("auto", "on", "off"),
        default="auto",
        help=(
            "interactive terminal UI: 'auto' (default) enables "
            "it when stdout is a TTY and --quiet is not set; "
            "'on' forces it; 'off' falls back to the JSON event "
            "stream"
        ),
    )
    parser.add_argument(
        "--color",
        choices=("auto", "always", "never"),
        default="auto",
        help=(
            "color output for the terminal UI: 'auto' (default) "
            "honors NO_COLOR and TERM; 'always' / 'never' force"
        ),
    )


#: Maps benchmark name -> {arg dest: long-form CLI flag} for options the
#: benchmark added via :meth:`Benchmark.register_options`. Populated at
#: ``parse_args()`` time by :func:`_register_benchmark_options` and read at
#: subinstance-launch time by :func:`_passthrough_argv`. The auto-passthrough
#: design assumes any option a benchmark registers is meaningful to the inner
#: schedbench invocation (controls how the benchmark runs); no per-option
#: opt-in is needed.
_PER_BENCH_PASSTHROUGH = {}


def _long_form_flag(action):
    """Return the canonical CLI flag for an argparse action.

    Prefers the first ``--long`` form (the convention for option names in
    record-keeping); falls back to the first option string if no long form is
    present (single-letter-only options, which the benchmarks don't currently
    use).
    """
    return next(
        (s for s in action.option_strings if s.startswith("--")),
        action.option_strings[0],
    )


def _register_benchmark_options(run_parser, bench_cls):
    """Add ``bench_cls``'s benchmark-specific options to ``run_parser``.

    If the benchmark inherits the no-op :meth:`Benchmark.register_options`
    default, no argument group is added (keeps the ``--help`` output clean
    for benchmarks that use only the common options). Otherwise an argument
    group titled ``"{name} options"`` is added, the benchmark's
    ``register_options`` is called against it, and the resulting actions are
    introspected to populate :data:`_PER_BENCH_PASSTHROUGH` for the launcher.

    The classmethod-inheritance check compares ``__func__`` rather than the
    bound methods directly: each ``cls.register_options`` access creates a
    fresh bound method, so ``cls.foo is Base.foo`` is always False even when
    the subclass inherits Base's implementation unchanged. The underlying
    ``__func__`` attribute is the same function object across both classes
    when no override is present.
    """
    if bench_cls.register_options.__func__ is Benchmark.register_options.__func__:
        _PER_BENCH_PASSTHROUGH[bench_cls.name] = {}
        return
    group = run_parser.add_argument_group(
        f"{bench_cls.name} options",
        description=bench_cls.description,
    )
    bench_cls.register_options(group)
    _PER_BENCH_PASSTHROUGH[bench_cls.name] = {
        action.dest: _long_form_flag(action)
        for action in group._group_actions
        if action.dest != "help" and action.option_strings
    }


def parse_args():
    parser = argparse.ArgumentParser(
        prog="flux-schedbench",
        formatter_class=help_formatter(),
        description=(
            "Run scheduler benchmarks against fake resources and "
            "save aggregate metrics"
        ),
    )
    sub = parser.add_subparsers(dest="subcommand")
    sub.required = True  # subparsers(required=) requires Python 3.7+

    # `flux schedbench run TEST ...`
    run_p = sub.add_parser(
        "run",
        help="run a benchmark",
        formatter_class=help_formatter(),
    )
    run_p.add_argument(
        "test",
        choices=sorted(BENCHMARKS.keys()),
        help="benchmark to run",
    )
    _add_common_run_opts(run_p)

    # Register per-benchmark options. Each benchmark class can add its
    # own flags via a register_options() classmethod; the framework
    # discovers them by walking the resulting argument group's actions
    # and uses that to drive subinstance passthrough (see
    # _PER_BENCH_PASSTHROUGH below). Benchmarks that use only the
    # common options inherit the no-op default and don't get a group
    # at all (keeps --help clean).
    for name in sorted(BENCHMARKS.keys()):
        _register_benchmark_options(run_p, BENCHMARKS[name])

    # `flux schedbench report ...`
    rep_p = sub.add_parser(
        "report",
        help="pretty-print results for a single benchmark",
        formatter_class=help_formatter(),
    )
    rep_p.add_argument(
        "test",
        choices=sorted(BENCHMARKS.keys()),
        metavar="TEST",
        help="benchmark to report on (rows of other tests are " "filtered out)",
    )
    rep_p.add_argument(
        "--results-file",
        default="./schedbench-results.json",
        metavar="PATH",
        help="results file path (default: ./schedbench-results.json)",
    )
    rep_p.add_argument(
        "--filter",
        action="append",
        default=[],
        metavar="KEY=VAL",
        help="show only runs matching KEY=VAL; may be given " "multiple times",
    )
    rep_p.add_argument(
        "-o",
        "--format",
        type=str,
        default="default",
        metavar="FORMAT",
        help="output format: a named format ('default', 'long', "
        "'csv', or any user-defined format from "
        "~/.config/flux/flux-schedbench-report-<test>.toml; "
        "'help' lists available names) or a literal "
        "Python-style format string",
    )
    rep_p.add_argument(
        "--sort",
        type=str,
        default="",
        metavar="KEY,...",
        help="sort by one or more keys (prefix with '-' for " "descending)",
    )
    rep_p.add_argument(
        "--no-header",
        action="store_true",
        help="omit the header row",
    )

    return parser.parse_args()


def _detect_scheduler(handle):
    """Return the name of the module currently providing the
    ``sched`` service, or ``None`` if no scheduler is loaded.

    Scheduler modules (``sched-simple``, ``sched-fluxion-qmanager``, etc.) all
    advertise the ``sched`` service. ``ModuleList`` indexes loaded modules by
    their advertised services, so this is the single source of truth for "what
    scheduler is the broker actually using right now" — independent of the CLI
    ``--scheduler`` argument, which only controls what the subinstance asks
    modprobe to load. Used in both modes so the recorded ``scheduler.name``
    always reflects broker reality (catches typos in ``--scheduler``, makes
    ``--exec`` records honest about what's loaded).
    """
    return ModuleList(handle).lookup("sched")


def _query_real_resources(handle):
    """Snapshot the broker's currently-up resources.

    Used in both --exec mode and inside an isolated subinstance to size
    benchmarks (e.g. fill-machine's saturation count) and fill the resources
    block in the results record. In --exec mode the broker has real
    hardware; inside the subinstance it has the synthetic R installed by the
    fake-resources rc1 task. The query is the same either way.

    Caveat: assumes uniform cores/GPUs per node, computing averages with floor
    division. Most HPC allocations are uniform; non-uniform setups will see
    rounded averages. ``saturation_count`` will accordingly under-count by up
    to one slot per heterogeneous node — bounded enough that aggregate-rate
    measurements remain meaningful.
    """
    rl = flux.resource.resource_list(handle).get()
    up = rl.up
    nodes = up.nnodes
    if nodes == 0:
        raise RuntimeError(
            "no resources are up on this broker; --exec needs "
            "an allocation with at least one online node"
        )
    return {
        "nodes": nodes,
        "cores_per_node": up.ncores // nodes,
        "gpus_per_node": up.ngpus // nodes,
    }


def _build_benchmark(args, resources):
    """Construct the benchmark instance from CLI args.

    Delegates to the benchmark class's :meth:`from_args` classmethod, which
    knows which CLI args and resource-shape values its constructor needs.
    ``resources`` is the resolved shape dict (real-broker query or fake
    resources via the rc1 task in the subinstance); ``from_args`` may use it
    (fill-machine computes saturation count) or ignore it (throughput is
    resource-agnostic).
    """
    return BENCHMARKS[args.test].from_args(
        args,
        resources,
        watcher_factory=_WATCHERS[args.watcher],
    )


class _ResultOnlyEmitter:
    """Emitter for ``--quiet`` on a non-TTY stdout.

    Silently drops every benchmark event except the final ``result``, which is
    printed as a single JSON object on stdout. Matches the public surface of
    TerminalEmitter and TestEventEmitter so cmd_run and the benchmark classes
    don't need to know which emitter is in use. Lifecycle and progress events
    are no-ops; errors flow through the normal LOGGER path and the process
    exit code, leaving stdout silent so consumers piping to e.g. ``jq`` see
    either a single result object or nothing.
    """

    def __init__(self, stream=None):
        self._stream = stream if stream is not None else sys.stdout

    def test_start(self, test_name, stages, config=None):
        pass

    def stage(self, stage, stage_index, total_stages):
        pass

    def progress(self, current, total, unit, rate=None):
        pass

    def warning(self, message):
        pass

    def info(self, message):
        pass

    def metric(self, name, value, unit=None):
        pass

    def log(self, message):
        pass

    def result(self, metrics):
        # Emit the bare metrics dict, not an event envelope. Downstream tools
        # (jq, plotting scripts) get a clean JSON object with no unwrapping
        # required.
        print(json.dumps(metrics), file=self._stream, flush=True)

    def test_complete(self, duration):
        pass

    def test_error(self, error):
        pass


def _select_emitter(args):
    """Pick the emitter based on resolved UI mode and --quiet.

    Two axes:

    1. ``--ui={auto,on,off}`` chooses between the live TerminalEmitter and the
    JSON-event TestEventEmitter. ``auto`` (the default) picks TerminalEmitter
    iff stdout is a TTY, otherwise the JSON stream — so a developer gets the
    rich UI in their terminal but a script redirecting stdout gets parseable
    JSON without configuration.

    2. ``--quiet`` narrows the output. On a TTY this still gives a
    human-readable summary — title, config, and the results block — but with
    no progress bars, stage lines, or INFO log messages. Off a TTY ``--quiet``
    produces a single JSON object (the metrics dict) and nothing else; errors
    go to stderr only.

    The TerminalEmitter consults its ``verbosity`` to decide whether to render
    mid-run; passing QUIET there gives the quiet-summary behavior. Off-TTY
    quiet uses the dedicated :class:`_ResultOnlyEmitter` rather than a
    verbosity tweak on TestEventEmitter because the goal is a single ``{...}``
    line on stdout, not a stripped-down event stream.
    """
    ui = args.ui
    if ui == "auto":
        ui = "on" if sys.stdout.isatty() else "off"

    if ui == "on":
        verbosity = QUIET if args.quiet else VERBOSE
        return TerminalEmitter(verbosity=verbosity, color=args.color)

    # ui == "off"
    if args.quiet:
        return _ResultOnlyEmitter()
    return TestEventEmitter(verbosity=NORMAL)


def _flux_version(handle):
    """Best-effort: return broker version string, or None if unavailable."""
    try:
        return handle.attr_get("version")
    except (OSError, AttributeError):
        return None


def _config_dict(args, resources, scheduler_name):
    """The test.start event's context['config']: human-readable
    parameters of this run.

    ``resources`` and ``scheduler_name`` are the resolved values from the
    broker (post-setup), not the CLI defaults. Building the dict from them
    rather than from ``args.nodes`` / ``args.scheduler`` keeps the
    TerminalEmitter header accurate under ``--exec``, where the CLI values are
    unspecified and the real ones come from :func:`_query_real_resources` and
    :func:`_detect_scheduler`.

    Per-benchmark fields (job count, slot shape, duration mode, etc.) come
    from :meth:`Benchmark.config_dict` so adding a new benchmark with custom
    config fields is a one-classmethod change on the benchmark class.
    """
    cfg = {
        "nodes": resources["nodes"],
        "cores_per_node": resources["cores_per_node"],
        "gpus_per_node": resources["gpus_per_node"],
        "scheduler": scheduler_name,
        "watcher": args.watcher,
        "real_exec": args.real_exec,
    }
    cfg.update(BENCHMARKS[args.test].config_dict(args))
    return cfg


def _run_record(args, resources, scheduler_name, metrics, flux_version):
    """Build the result-file record for this run.

    ``resources`` carries the resolved shape (real or fake);
    ``scheduler_name`` is the broker's currently-loaded sched module, looked
    up post-setup (so it matches what actually ran, not what was requested);
    ``args.real_exec`` flags which mode produced the run. Schema is identical
    for both modes so reports and downstream tools don't have to branch.
    """
    return {
        "test_name": args.test,
        "tag": args.tag,
        "host": socket.gethostname(),
        "user": getpass.getuser(),
        "schedbench_version": SCHEDBENCH_VERSION,
        "flux_core_version": flux_version,
        "real_exec": args.real_exec,
        "scheduler": {
            "name": scheduler_name,
            "options": args.scheduler_options or "",
            "version": None,
        },
        "resources": {
            **resources,
            "hwloc_xml_path": args.hwloc_xml_path or "",
            "amend_r": args.amend_r or "",
        },
        "watcher": args.watcher,
        "benchmarks": {
            args.test: {"results": metrics},
        },
    }


#: Argparse dest -> CLI flag for common args that should be passed verbatim
#: to the inner schedbench invocation when launching a subinstance.
#: Resource-shape args (nodes, cores_per_node, gpus_per_node), --scheduler,
#: --conf, and --extra-start-options are consumed at the launcher level
#: (encoded into --conf= or appended to flux start argv) and are NOT in
#: this table.
#: --scheduler-options, --hwloc-xml-path, and --amend-r are passed through
#: for the JSON record only — the subinstance broker already has them baked
#: into its config; the inner schedbench just stores the user's intent in
#: the run record so reports can show which run used which topology /
#: amender. Per-benchmark options are passed through automatically via
#: :data:`_PER_BENCH_PASSTHROUGH` and don't need to be listed here.
_PASSTHROUGH = {
    "watcher": "--watcher",
    "tag": "--tag",
    "results_file": "--results-file",
    "ui": "--ui",
    "color": "--color",
    "njobs": "--njobs",
    "slot_cores": "--slot-cores",
    "slot_gpus": "--slot-gpus",
    "scheduler_options": "--scheduler-options",
    "hwloc_xml_path": "--hwloc-xml-path",
    "amend_r": "--amend-r",
}

#: Argparse dest -> CLI flag for boolean args. Emitted in the inner
#: invocation only when the boolean is True.
_PASSTHROUGH_FLAGS = {
    "no_save": "--no-save",
    "quiet": "-q",
    "verbose": "-v",
}


def _passthrough_argv(args):
    """Reconstruct argv for the inner schedbench invocation.

    Emits the subcommand and TEST positional, then walks the common-option
    passthrough table, the per-benchmark passthrough table (populated at
    parse-args time from each benchmark's :meth:`register_options`), and
    finally the boolean-flag table. Empty-string values are skipped (they
    would be no-ops anyway). Boolean flags are emitted only when set.
    """
    argv = [args.subcommand, args.test]

    def emit(table):
        """Append ``--flag VALUE`` pairs for every non-empty entry in
        ``table`` (a dest->flag mapping)."""
        for dest, flag in table.items():
            value = getattr(args, dest, None)
            if value is None or value == "":
                continue
            argv.extend([flag, str(value)])

    emit(_PASSTHROUGH)
    # Per-benchmark options, discovered at parse-args time. Anything the
    # active benchmark's register_options() added flows through verbatim.
    emit(_PER_BENCH_PASSTHROUGH.get(args.test, {}))

    for dest, flag in _PASSTHROUGH_FLAGS.items():
        if getattr(args, dest, False):
            argv.append(flag)
    return argv


def _launch_isolated(args):
    """Re-exec under ``flux start --conf=fake-resources.* ...`` so the
    benchmark runs against a fresh subinstance with synthetic resources.

    The launched ``flux start`` invocation:

    * sets ``[fake-resources]`` config keys from --nodes / --cores-per-node /
      --gpus-per-node so the fake-resources modprobe rc1 task installs
      synthetic R at broker startup (see flux-config-fake-resources(5));
    * sets ``modules.alternatives.sched`` when --scheduler is non-default,
      so the chosen scheduler module loads in place of sched-simple;
    * sets ``modules.sched.args`` from --scheduler-options (shlex-parsed
      into a TOML inline array) when provided;
    * appends any --extra-start-options verbatim (shlex-parsed) for
      escape-hatch use (broker attrs, additional conf, etc).

    The inner schedbench invocation is rebuilt from the parsed args via
    :func:`_passthrough_argv`, with :data:`_ISOLATED_ENV_VAR` set in env
    to short-circuit recursive launching. The current process is replaced
    via :func:`os.execvpe`; this function does not return.
    """
    conf_flags = [
        f"--conf=fake-resources.nnodes={args.nodes}",
        f"--conf=fake-resources.cores-per-node={args.cores_per_node}",
        f"--conf=fake-resources.gpus-per-node={args.gpus_per_node}",
    ]
    if args.hwloc_xml_path:
        conf_flags.append(f"--conf=fake-resources.hwloc-xml-path={args.hwloc_xml_path}")
    if args.amend_r:
        conf_flags.append(f"--conf=fake-resources.amend-r={args.amend_r}")
    if args.scheduler != "sched-simple":
        conf_flags.append(f"--conf=modules.alternatives.sched={args.scheduler}")
    if args.scheduler_options:
        # shlex-split user input into a TOML inline array. json.dumps
        # produces a string of the form '["opt1", "opt2"]' which is
        # syntactically valid TOML for an inline array of strings.
        opts_list = shlex.split(args.scheduler_options)
        conf_flags.append(f"--conf=modules.sched.args={json.dumps(opts_list)}")

    # User-supplied --conf entries. Each value already carries its
    # own KEY=VALUE; we prepend the --conf= prefix here so flux start
    # sees them as ordinary config flags. These layer on top of the
    # schedbench-managed entries above — TOML merge order is "later
    # wins", so a user --conf= that targets a key schedbench also
    # writes will override it.
    conf_flags.extend(f"--conf={entry}" for entry in args.conf)

    extra = []
    for raw in args.extra_start_options:
        extra.extend(shlex.split(raw))

    cmd = [
        "flux",
        "start",
        "-s",
        "1",
        *conf_flags,
        *extra,
        "--",
        "flux",
        "schedbench",
        *_passthrough_argv(args),
    ]
    env = {**os.environ, _ISOLATED_ENV_VAR: "1"}

    if args.verbose:
        LOGGER.info(
            "launching: %s",
            " ".join(shlex.quote(a) for a in cmd),
        )

    try:
        os.execvpe(cmd[0], cmd, env)
    except OSError as exc:
        raise RuntimeError(
            f"failed to launch `{cmd[0]}`: {exc}; " f"is the flux command in PATH?"
        )


def cmd_run(args):
    """Execute a single benchmark run.

    Three dispatch paths:

    * ``--exec``: run real jobs in the user's current broker. No
      subinstance launch; the broker's currently-loaded scheduler and
      currently-up resources are used as-is.

    * ``FLUX_SCHEDBENCH_ISOLATED`` set in env (see :data:`_ISOLATED_ENV_VAR`):
      we are the inner schedbench invocation launched by an outer call. The
      surrounding broker has the configured fake resources installed by the
      fake-resources modprobe rc1 task; run the benchmark in it.

    * Neither: launch a fake-resources subinstance via ``flux start`` and
      re-exec this command inside it. See :func:`_launch_isolated`.
    """
    if args.real_exec or os.environ.get(_ISOLATED_ENV_VAR):
        _run_in_broker(args)
    else:
        _launch_isolated(args)  # does not return


def _run_in_broker(args):
    """Run the benchmark in the current broker.

    The broker may have real or fake resources; either way, we query its
    state via the resource module and run the benchmark against what's
    there. ``args.real_exec`` controls whether the benchmark uses mock
    or real job execution; the resources block in the JSON record
    reflects whatever the broker actually has.
    """
    handle = flux.Flux()
    resources = _query_real_resources(handle)

    scheduler_name = _detect_scheduler(handle)
    if scheduler_name is None:
        raise RuntimeError(
            "no scheduler module is loaded (no module provides "
            "the 'sched' service). With --exec, load one before "
            "running (e.g. `flux module load sched-simple`); "
            "without --exec, this should have been arranged by "
            "the fake-resources rc1 task — check the broker's "
            "modprobe configuration."
        )

    bench = _build_benchmark(args, resources)

    emitter = _select_emitter(args)
    # The metrics-table spec is a TerminalEmitter-only concern (other
    # emitters carry the full result dict over their event stream
    # for downstream tools to format).  hasattr opt-in keeps event
    # emitters in flux.testing.events independent of schedbench-UI
    # specifics.
    if hasattr(emitter, "set_summary_metrics"):
        emitter.set_summary_metrics(type(bench).SUMMARY_METRICS)
    emitter.test_start(
        args.test,
        stages=bench.stages,
        config=_config_dict(args, resources, scheduler_name),
    )

    t0 = time.monotonic()
    try:
        metrics = bench.run(handle, emitter)
    except Exception as exc:  # noqa: BLE001
        emitter.test_error(str(exc))
        raise
    duration = time.monotonic() - t0

    emitter.result(metrics)
    emitter.test_complete(duration=duration)

    if not args.no_save:
        results = BenchmarkResults(args.results_file)
        results.add_run(
            _run_record(
                args,
                resources,
                scheduler_name,
                metrics,
                _flux_version(handle),
            ),
        )
        results.save()


def _parse_filters(filter_args):
    """Parse --filter KEY=VAL strings into a list of (key, val) pairs."""
    parsed = []
    for f in filter_args:
        if "=" not in f:
            raise ValueError(f"--filter must be KEY=VAL form, got: {f!r}")
        key, val = f.split("=", 1)
        parsed.append((key, val))
    return parsed


def _matches_filters(run, filters):
    """Does ``run`` match every ``(key, val)`` filter?

    Keys may be dotted to reach into nested dicts via
    :func:`flux.util.get_treedict`. The JSON schema groups related
    fields under ``scheduler``, ``resources``, etc., and filters need
    to reach into those groups (e.g. ``scheduler.name=sched-simple``,
    ``resources.nodes=100``). Values are compared as strings since
    they come from the CLI that way — the JSON record's bools, ints,
    and strings all get ``str()``-rendered before comparison.
    """
    for key, val in filters:
        if str(get_treedict(run, key)) != val:
            return False
    return True


class _BenchmarkReportConfig(UtilConfig):
    """User-customizable named-format registry for one benchmark.

    Wraps :class:`flux.util.UtilConfig` so the benchmark's builtin
    ``REPORT_FORMATS`` are available to :meth:`get_format_string` along with
    any user-defined formats loaded from
    ``~/.config/flux/flux-schedbench-report-<benchmark>.toml``. Each benchmark
    gets its own config namespace so users adding a ``myformat`` for
    throughput don't pollute fill-machine's report.

    Auto-generates a ``csv`` format from the benchmark's ``REPORT_HEADINGS``
    when the benchmark doesn't define one explicitly: this keeps CSV output in
    sync with the field schema without per-benchmark duplication. Benchmarks
    that want different column ordering can override ``csv`` in their
    ``REPORT_FORMATS``.
    """

    def __init__(self, bench_cls):
        formats = dict(bench_cls.REPORT_FORMATS)
        if "csv" not in formats:
            formats["csv"] = {
                "description": "All fields, comma-separated (for plotting)",
                "format": _csv_format_for(bench_cls.REPORT_HEADINGS),
            }
        super().__init__(
            name=f"flux-schedbench-report-{bench_cls.name}",
            initial_dict={"formats": formats},
        )

    def validate(self, path, config):
        for key, value in config.items():
            if key == "formats":
                self.validate_formats(path, value)
            else:
                raise ValueError(f"{path}: invalid key {key}")


def _csv_format_for(headings):
    """Build a CSV format string from a benchmark's headings.

    Returns ``"{f1},{f2},...,{fN}"`` for each key in headings, preserving
    insertion order. No format specs are used so missing/None values render as
    empty cells (per ``""`` in :data:`OutputFormat.empty_outputs`), which is
    exactly what spreadsheets and pandas expect.
    """
    return ",".join(f"{{{k}}}" for k in headings)


class _ReportRow:
    """Flat attribute view of a results-file run for OutputFormat.

    Every field named in the benchmark's ``REPORT_HEADINGS`` is initialized to
    ``""`` so OutputFormat's ``?:`` sentinel works (the empty-string check in
    ``empty_outputs()`` picks it up) and CSV cells for missing data render
    empty rather than ``"None"``. Values are then overwritten from the run
    record where applicable. The row never raises ``AttributeError`` for a
    heading-listed field — older results that predate a metric just get ``""``
    for it.
    """

    def __init__(self, run, bench_cls):
        for key in bench_cls.REPORT_HEADINGS:
            setattr(self, key, "")
        self.time = run.get("iso_timestamp", "")
        sched = run.get("scheduler") or {}
        self.scheduler = sched.get("name", "")
        self.tag = run.get("tag", "")
        self.watcher = run.get("watcher", "")
        res = run.get("resources") or {}
        self.nodes = res.get("nodes", "")
        self.cores = res.get("cores_per_node", "")
        self.gpus = res.get("gpus_per_node", "")
        # real_exec is a boolean in the JSON; rendered as Y/N so the column
        # reads at a glance and is consistent in CSV too (no NaN cells for
        # pandas). Older records without the field render as "N" — correct,
        # since the feature postdates them and those runs were all mock.
        self.real_exec = "Y" if run.get("real_exec") else "N"
        # Per-test metric dict lives at benchmarks[test_name].results. Use the
        # run's recorded test_name (not bench_cls.name) so a rename of the
        # benchmark class doesn't strand old records.
        test = run.get("test_name", "")
        metrics = (run.get("benchmarks") or {}).get(test, {}).get("results", {})
        for key, value in metrics.items():
            if key in bench_cls.REPORT_HEADINGS:
                setattr(self, key, value)

        # Final normalization: convert any None to "". Two ways None leaks
        # into a row: (a) argparse stores unset string options like --tag as
        # None, which _run_record propagates to the JSON; .get(key, default)
        # only returns default when the key is *absent*, not when its value is
        # explicitly None — so tag-less runs leave self.tag=None here even
        # though we passed "" as the default. (b) An older results file may
        # have a metric stored as None. Both cases would otherwise blow up at
        # format time because numeric/precision specs reject None (and even
        # the string spec "<8.8" fails with "unsupported format string passed
        # to NoneType.__format__"). Defending here keeps the renderer simple
        # and tolerant.
        for key in bench_cls.REPORT_HEADINGS:
            if getattr(self, key) is None:
                setattr(self, key, "")


def cmd_report(args):
    """Pretty-print results for a single benchmark.

    Loads the benchmark's report config (builtin + user TOML), resolves ``-o``
    against named formats, applies ``--sort`` and ``--filter``, and delegates
    to :meth:`OutputFormat.print_items` for rendering. An implicit filter
    restricts rows to the requested benchmark so the output is always per-test
    (no cross-benchmark column confusion).
    """
    bench_cls = BENCHMARKS[args.test]

    results = BenchmarkResults(args.results_file)
    runs = results.get_runs()

    filters = _parse_filters(args.filter)
    filters.append(("test_name", args.test))
    runs = [r for r in runs if _matches_filters(r, filters)]

    if not runs:
        LOGGER.error(
            "no %s runs match the given filters",
            args.test,
        )
        sys.exit(1)

    config = _BenchmarkReportConfig(bench_cls).load()
    try:
        fmt_str = config.get_format_string(args.format)
        formatter = OutputFormat(
            fmt_str,
            headings=bench_cls.REPORT_HEADINGS,
        )
    except ValueError as err:
        raise ValueError(f"invalid format: {err}")

    if args.sort:
        try:
            formatter.set_sort_keys(args.sort)
        except ValueError as err:
            raise ValueError(f"invalid sort key: {err}")

    rows = [_ReportRow(r, bench_cls) for r in runs]
    formatter.print_items(rows, no_header=args.no_header)


@flux.util.CLIMain(LOGGER)
def main():
    args = parse_args()
    # `run` accepts -q/--quiet but `report` does not, so guard the attribute
    # lookup. When set, raise the log level past INFO so callback-based info
    # logging from helpers goes silent. WARNING and above still pass through
    # — those are real issues the user needs to see regardless of quiet mode.
    # Results-bearing events flow through the emitter, not the logger, so the
    # result event is unaffected.
    if getattr(args, "quiet", False):
        LOGGER.setLevel(logging.WARNING)
    if args.subcommand == "run":
        cmd_run(args)
    elif args.subcommand == "report":
        cmd_report(args)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
