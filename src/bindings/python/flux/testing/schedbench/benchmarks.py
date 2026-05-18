###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Built-in scheduler benchmarks for flux-schedbench.

This module composes the lower-level testing infrastructure
(:mod:`flux.testing.bulkrun`, :mod:`flux.testing.fake_resources`,
:mod:`flux.testing.events`) into a registry of ready-to-run benchmarks.

Each benchmark subclasses :class:`Benchmark` and provides:

- a ``name`` class attribute (the ``TEST`` positional for
  ``flux schedbench run``);
- a ``stages`` tuple (phase names emitted via ``emitter.stage()``);
- ``REPORT_HEADINGS`` and ``REPORT_FORMATS`` class attributes for
  ``flux schedbench report``;
- a :meth:`Benchmark.run` body;
- :meth:`Benchmark.from_args` to construct from CLI args;
- optionally :meth:`Benchmark.register_options` to add
  benchmark-specific CLI flags (auto-passed through to subinstance
  launches), and :meth:`Benchmark.config_dict` for per-run config
  shown in the TerminalEmitter header.

The :data:`BENCHMARKS` registry maps a benchmark's ``name`` to its
class for CLI lookup. New benchmarks defined in this module are
added to that dict at the bottom; benchmarks defined in peer
modules (e.g. :mod:`flux.testing.schedbench.locality`) are merged
in by the package ``__init__``.

Rate measurement convention
---------------------------

Every benchmarked duration in these classes uses ``t_submit_start`` (the
script-side wall-clock at the start of ``bulk.run()``) as its **left
endpoint**. The broker's ``submit`` event is emitted by the ``job-ingest``
service, *not* the broker proper: when the user calls
``flux.job.submit_async()``, the RPC arrives at ``job-ingest``, which then
runs the jobspec through its pipeline (validation, optional jobspec
transformations, userid checks) and **only then** posts the ``submit`` event.
``result.first_event_t("submit")`` is therefore the moment the *first* job
finished ingest processing — not the moment the script started submitting. The
ingest pipeline is real work the user paid for; a benchmark that excluded it
would understate how long submission actually took (and inflate the
corresponding rates). Using ``t_submit_start`` captures every millisecond from
"user pressed enter" forward.

The **right endpoints** for the headline rates (``submit_rate``,
``alloc_rate``, ``start_rate``, ``cancel_rate``) are script-observed
(``t_all_X``), not broker-side (``last_event_t``). Reason: the user-relevant
question is "how fast did all jobs reach phase X *as far as my program could
tell*", which includes the watcher-delivery latency between the broker
emitting the last event and the script receiving it. The broker-emit times are
still recorded as ``t_last_X_event`` so downstream tooling can derive either
window.

The one exception is ``ingest_rate`` (throughput and fill-machine), which
intentionally uses ``last_event_t("submit")`` on the right endpoint. It's a
separate, meaningful measurement — "how fast can job-ingest accept
submissions" — independent of watcher overhead. Pair it with ``submit_rate``
to size the watcher-delivery delay (e.g. when comparing journal vs per-job
watchers).

``throughput`` uses ``last_event_t("clean")`` on the right so it matches the
broker-side definition users expect to compare across systems; its
script-observed counterpart is ``script_throughput``, derived from
``result.script_runtime``. This deliberately diverges from
``src/test/throughput.py``'s definition (which uses
``first_event_t("submit")`` on the left and so excludes the first job's ingest
processing time): the two numbers will not match, and ours is the more honest
representation of end-to-end work.
"""

import time
from abc import ABC, abstractmethod

from flux.idset import IDset
from flux.job import JobspecV1
from flux.testing.bulkrun import BulkRun
from flux.testing.fake_resources import saturation_count

#: Registry of benchmark name → class. Populated automatically by
#: :meth:`Benchmark.__init_subclass__` when a concrete subclass is
#: defined (any class with a non-None ``name`` attribute). New
#: benchmarks are just a subclass of :class:`Benchmark` — no manual
#: registration step is required, regardless of which module the
#: subclass lives in.
BENCHMARKS = {}


class Benchmark(ABC):
    """Base class for flux-schedbench benchmarks.

    Subclasses customize four pieces of behavior:

    - :attr:`name` — short slug used as the ``TEST`` positional for
      ``flux schedbench run`` and as the key in :data:`BENCHMARKS`.
    - :attr:`stages` — ordered tuple of phase names emitted to the UI
      via ``emitter.stage()``. Drives the progress bar's stage
      indicator.
    - :class:`REPORT_HEADINGS` and :class:`REPORT_FORMATS` (class
      attributes) — column metadata for ``flux schedbench report``.
    - :meth:`run` — the actual benchmark body.

    Three optional classmethods customize how the CLI surface
    plumbs through to the benchmark:

    - :meth:`register_options` — add benchmark-specific argparse
      options. Default: no-op (the benchmark uses only the common
      options on ``flux schedbench run``). Options registered here
      are automatically passed through to subinstance launches.
    - :meth:`from_args` — construct an instance from parsed argv
      plus the broker-resolved cluster shape. Must be overridden;
      the default raises ``NotImplementedError``.
    - :meth:`config_dict` — return per-benchmark fields to include
      in the ``test.start`` event's ``context['config']`` dict (for
      the TerminalEmitter's run-parameter display). Default: empty.
    """

    name = None
    stages = ()
    description = None  # optional human prose shown in --help group header

    #: The benchmark's canonical headline outcome, as a
    #: ``(metric_key, unit)`` tuple — e.g. ``("throughput", "job/s")``
    #: or ``("mean_locality_score", "")``.  The sweep dashboard uses
    #: this to render each row's "result=N unit" column.
    #:
    #: Subclasses should override.  ``None`` (the default) means the
    #: sweep falls back to a heuristic pick from SUMMARY_METRICS.
    RESULT = None

    #: Ordered tuple of ``(result_key, label, kind)`` triples declaring
    #: which result-dict keys appear in the post-run metrics table and
    #: how they're rendered. ``kind`` selects a formatter / unit in
    #: :func:`flux.testing.schedbench.ui._format_metric` — currently
    #: ``"count"``, ``"rate"``, ``"seconds"``, ``"fraction"``. Keys
    #: absent from a given result dict are silently skipped, so this
    #: tuple can mention metrics that only some runs produce.
    #:
    #: Subclasses must redefine this with their own headline metrics.
    #: Order matters because the table renders top-to-bottom: put the
    #: headline metric near the top. Empty default catches the
    #: "forgot to declare" case at run-time as an empty metrics
    #: table.
    SUMMARY_METRICS = ()

    def __init_subclass__(cls, **kwargs):
        """Auto-register concrete subclasses in :data:`BENCHMARKS`.

        A subclass with a non-None :attr:`name` is treated as a
        concrete benchmark and added to the registry by name.
        Abstract intermediates (e.g. a shared base class for related
        benchmarks) leave :attr:`name` as ``None`` and skip
        registration.

        Re-registering the same class (e.g. via ``importlib.reload``)
        is idempotent: the identity check distinguishes a reload from
        a real collision. Genuine duplicates (different class, same
        name) raise so silent shadowing between modules is caught at
        import time.

        Subclasses that forget to implement an abstract method still
        register here (Python populates ``__abstractmethods__`` after
        ``__init_subclass__`` returns), but ``abc.ABC`` makes them
        fail at instantiation with a clear message about which
        method is missing.
        """
        super().__init_subclass__(**kwargs)
        if cls.name is None:
            return
        existing = BENCHMARKS.get(cls.name)
        if existing is cls:
            return  # idempotent re-registration (e.g. importlib.reload)
        if existing is not None:
            raise RuntimeError(
                f"duplicate benchmark name: {cls.name!r} "
                f"(already registered by {existing.__name__}, "
                f"new {cls.__name__})"
            )
        BENCHMARKS[cls.name] = cls

    @classmethod
    def register_options(cls, group):
        """Add this benchmark's argparse options to ``group``.

        Default no-op: the benchmark uses only the common options that
        ``flux schedbench run`` defines for every benchmark (``-N``,
        ``--njobs``, ``--slot-cores``, etc.). Subclasses with unique
        options override this method and call ``group.add_argument()``
        as usual; the framework discovers the registered flags by
        introspection and includes them in the subinstance-launch
        passthrough.

        Args:
            group: an :class:`argparse._ArgumentGroup` created by the
                framework with title ``"{name} options"``.
        """

    @classmethod
    @abstractmethod
    def from_args(cls, args, resources, watcher_factory):
        """Construct an instance from parsed argv.

        Args:
            args: the :class:`argparse.Namespace` from
                ``parse_args()``. Free to read any attribute the
                class's :meth:`register_options` added, plus the
                shared ones (``njobs``, ``slot_cores``, ``slot_gpus``,
                ``real_exec``, etc.).
            resources: shape dict from ``_query_real_resources``
                (keys ``nodes``, ``cores_per_node``, ``gpus_per_node``).
                Use to compute saturation counts or check for the
                presence of GPUs. Ignore if the benchmark is purely
                user-driven.
            watcher_factory: callable taking a flux handle and
                returning a :class:`JobEventWatcher`. Pass through to
                the benchmark constructor's ``watcher_factory=``
                argument.
        """

    @abstractmethod
    def run(self, handle, emitter):
        """Run the benchmark; return the result dict.

        Args:
            handle: a connected :class:`flux.Flux` handle.
            emitter: the UI emitter; call ``.stage(name, idx, total)``
                at phase boundaries and forward progress events to
                whatever stages :attr:`stages` declares.

        Returns:
            A dict of result keys to scalar values. Keys named in
            :attr:`SUMMARY_METRICS` appear in the post-run table;
            other keys are preserved in the results JSON for later
            analysis (timestamps, distributions, etc.).
        """

    @classmethod
    def config_dict(cls, args):
        """Return per-benchmark fields for the ``test.start`` event's
        ``context['config']`` dict.

        Used by the TerminalEmitter to display run parameters in
        the header. Common fields (nodes, scheduler, watcher,
        real_exec) are added by the framework; this method
        contributes only fields specific to the benchmark.

        Default: empty dict. Subclasses with notable per-run
        parameters (job count, slot shape, duration, etc.) override.
        """
        return {}


def simple_jobspec(
    slot_cores=1, slot_gpus=0, mock=True, mock_run_duration="0.001s", command=("true",)
):
    """Build a minimal :class:`JobspecV1` for a one-task slot.

    Two execution modes:

    * ``mock=True`` (default): the broker simulates execution
      via the ``system.exec.test.run_duration`` attribute.
      ``command`` is irrelevant because the test exec
      implementation never invokes it; the default
      ``("true",)`` is fine. ``mock_run_duration`` controls how
      long the broker pretends the job runs; the special value
      ``"0"`` makes the simulated job never finish on its own
      (FillMachineBenchmark uses this so cancellation drives
      termination).

    * ``mock=False``: the broker really runs ``command`` and
      no test-duration attribute is set. Throughput-style
      benchmarks default to ``("true",)`` for a near-zero real
      duration; benchmarks that need long-lived jobs (e.g.
      fill-machine real mode) pass ``("sleep", "inf")`` and
      rely on cancellation.

    Args:
        slot_cores: Cores per task. Default 1.
        slot_gpus: GPUs per task. Default 0 (the attribute is omitted entirely
            from the request when zero).
        mock: Select mock vs real execution per above.
        mock_run_duration: Simulated duration string (e.g. ``"0.001s"`` or
            ``"0"``). Ignored when ``mock`` is False.
        command: Argv. Used as-is when ``mock=False``; the broker ignores it
            when ``mock=True`` but it's still recorded on the jobspec.

    Returns:
        :class:`flux.job.JobspecV1` configured per the arguments.
    """
    kwargs = {"cores_per_task": slot_cores}
    if slot_gpus:
        kwargs["gpus_per_task"] = slot_gpus
    if mock:
        kwargs["attributes"] = {
            "system.exec.test.run_duration": mock_run_duration,
        }
    return JobspecV1.from_submit(list(command), **kwargs)


#: Maximum number of unique-error groups to enumerate in a diagnostic message.
#: The full list is available on the BulkRunResult; this just caps the message
#: size.
_DIAGNOSTIC_GROUP_CAP = 10


#: Heading labels common to every benchmark's report output. Each benchmark
#: class extends this dict in its own ``REPORT_HEADINGS`` with metrics
#: specific to that benchmark (e.g. ``throughput`` for ThroughputBenchmark,
#: ``time_to_fill`` for FillMachineBenchmark). Centralizing shared columns
#: here keeps the abbreviations consistent across reports — SCHED always means
#: scheduler, JOBS always means job count, etc. The 'test' field is
#: intentionally absent: ``flux schedbench report`` requires a TEST argument
#: so a TEST column would be redundant on every row.
COMMON_REPORT_HEADINGS = {
    "time": "TIME",
    "scheduler": "SCHED",
    "tag": "TAG",
    "watcher": "WATCHER",
    "nodes": "NODES",
    "cores": "CORES",
    "gpus": "GPUS",
    "njobs": "JOBS",
    "submit_rate": "SUBMIT",
    "ingest_rate": "INGEST",
    "alloc_rate": "ALLOC",
    "real_exec": "REAL",
}


class Tracker:
    """Per-event-type counter that drives UI progress emission.

    The progress event pattern in both benchmarks repeats: count incoming
    events of a given type, emit a progress event at every Nth count plus on
    the final count, and (sometimes) defer emission until a phase boundary has
    been crossed (so an early event doesn't overwrite the wrong stage's bar).
    This class encapsulates that pattern.

    Args: total: expected final count (typically ``njobs``). label: unit
    string passed to ``emitter.progress`` (e.g. ``"job"``, ``"cancel"``).
    gate: optional zero-arg predicate; when it returns False, :meth:`step`
    increments the counter but suppresses progress emission. Used to defer
    emission until a phase boundary has flipped some condition true (e.g.
    ``lambda: t_all_submitted[0] is not None``).
    """

    #: Emit progress every this many steps (plus the final one). Keeps the
    #: redraw rate sane at scale: 8192 jobs produce 82 progress events per
    #: phase, comfortably within the TerminalEmitter's 20Hz throttle.
    _STEP_INTERVAL = 100

    def __init__(self, total, label, gate=None):
        self.count = 0
        self.total = total
        self.label = label
        self._gate = gate

    def step(self, emitter):
        """Increment the counter; emit a progress event if the
        gate (if any) is open and the count hits an interval boundary or the
        final value."""
        self.count += 1
        if self._gate is not None and not self._gate():
            return
        if self.count % self._STEP_INTERVAL == 0 or self.count == self.total:
            emitter.progress(self.count, self.total, self.label)

    def flush(self, emitter):
        """Emit current count as a progress event without
        stepping. Called at a phase boundary when the gate has just flipped
        open, to surface any progress that accumulated while it was closed."""
        if self.count > 0:
            emitter.progress(self.count, self.total, self.label)


def failure_diagnostic(result, header):
    """Build a multi-line error message from broker-reported causes.

    ``header`` is the first line (the benchmark's description of what went
    wrong). Subsequent lines list job counts at each lifecycle phase, the
    OSError strings from BulkRun's submit_async failures
    (``result.submit_failures``), and the broker's note from each recorded
    ``"exception"`` event (which requires ``"exception"`` in
    ``events_of_interest``). The caller raises a ``RuntimeError`` with the
    returned string; this function just formats it.

    Identical errors are de-duplicated: submit failures are grouped by error
    string and the submission indices that hit each are encoded as an
    :class:`flux.idset.IDset` (e.g. ``0-1023: <error>`` rather than 1024
    identical lines). Exception events are grouped by (type, severity, note)
    and shown as a count plus a sample jobid.
    """
    lines = [header]

    n_requested = result.submit_attempted
    n_submit_failed = len(result.submit_failures)
    n_submitted = result.njobs
    lines.append(
        "  jobs: {0} requested, {1} submitted, {2} submit-failed".format(
            n_requested,
            n_submitted,
            n_submit_failed,
        )
    )
    if n_submitted > 0:
        for event_name in ("alloc", "start", "clean"):
            n = len(result.jobids_with(event_name))
            lines.append(
                "  {0}/{1} reached event {2!r}".format(
                    n,
                    n_submitted,
                    event_name,
                )
            )

    if result.submit_failures:
        # Group by error string; encode each group's submission indices as an
        # idset. Most-common error first.
        groups = {}
        for f in result.submit_failures:
            groups.setdefault(f["error"], []).append(f.get("submit_index", -1))
        ordered = sorted(
            groups.items(),
            key=lambda kv: (-len(kv[1]), kv[0]),
        )
        lines.append("Submit failures (from submit_async response):")
        for err, indices in ordered[:_DIAGNOSTIC_GROUP_CAP]:
            valid = [i for i in indices if i >= 0]
            if valid:
                idset = IDset()
                for i in valid:
                    idset.set(i)
                prefix = str(idset)
            else:
                # Backward-compat: BulkRun without submit_index support — just
                # show count.
                prefix = "{0} submissions".format(len(indices))
            lines.append("  {0}: {1}".format(prefix, err))
        extra = len(ordered) - _DIAGNOSTIC_GROUP_CAP
        if extra > 0:
            lines.append(
                "  ... and {0} more unique error(s)".format(extra),
            )

    exception_jobids = result.jobids_with("exception")
    if exception_jobids:
        # Group by (type, severity, note); show count plus a sample jobid.
        # Jobids in flux aren't contiguous so an idset isn't useful here — a
        # count + one example is enough to look the job up in the eventlog.
        groups = {}
        for jid in exception_jobids:
            ev = result.jobs[jid]["exception"]
            ctx = getattr(ev, "context", {}) or {}
            key = (
                ctx.get("type", "?"),
                ctx.get("severity", "?"),
                ctx.get("note", ""),
            )
            groups.setdefault(key, []).append(jid)
        ordered = sorted(
            groups.items(),
            key=lambda kv: (-len(kv[1]), kv[0]),
        )
        lines.append("Job exceptions (from main eventlog):")
        for (etype, severity, note), jobids in ordered[:_DIAGNOSTIC_GROUP_CAP]:
            lines.append(
                "  {0} job(s) (sample jobid {1}): "
                "type={2} severity={3} note={4!r}".format(
                    len(jobids),
                    jobids[0],
                    etype,
                    severity,
                    note,
                )
            )
        extra = len(ordered) - _DIAGNOSTIC_GROUP_CAP
        if extra > 0:
            lines.append(
                "  ... and {0} more unique exception(s)".format(extra),
            )

    if not result.submit_failures and not exception_jobids:
        lines.append(
            "No submit failures or exception events recorded. "
            "Jobs may be stuck pending; check `flux jobs -a`."
        )

    return "\n".join(lines)


class ThroughputBenchmark(Benchmark):
    """Burst-submit N tiny jobs and measure aggregate throughput.

    Mirrors the metric set produced by ``src/test/throughput.py``
    (``throughput`` and ``script_throughput``) and adds ``submit_rate`` and
    ``alloc_rate`` so a slow scheduler isn't masked by a fast broker or vice
    versa.

    The benchmark uses the default :class:`BulkRun` lifecycle (wait for all
    ``clean`` events, then exit the reactor); no bulk-event callbacks are
    wired. Progress is counted against ``clean`` events.

    Uses only the common ``flux schedbench run`` options (``--njobs``,
    ``--slot-cores``, ``--slot-gpus``); :meth:`register_options` inherits the
    no-op default.
    """

    name = "throughput"
    stages = ("submit", "execute")

    #: ``throughput`` is the headline (broker-driven jobs/sec measured
    #: through to ``clean``); ``script_throughput`` is the script-wall
    #: cousin (includes overhead of the benchmark process itself).
    SUMMARY_METRICS = (
        ("njobs", "jobs", "count"),
        ("throughput", "throughput", "rate"),
        ("script_throughput", "script throughput", "rate"),
        ("submit_rate", "submit rate", "rate"),
        ("alloc_rate", "alloc rate", "rate"),
        ("ingest_rate", "ingest rate", "rate"),
    )

    RESULT = ("throughput", "job/s")

    #: Report-only metadata. ``REPORT_HEADINGS`` lists every field that may
    #: appear in a ``flux schedbench report throughput`` output, mapping the
    #: field name (as referenced in format strings) to its column header.
    #: Identity fields are inherited from :data:`COMMON_REPORT_HEADINGS`;
    #: metrics specific to this benchmark are listed inline.
    #: ``REPORT_FORMATS`` provides named output formats consumed by
    #: ``UtilConfig.get_format_string()`` — see :func:`_BenchmarkReportConfig`
    #: in flux-schedbench.py. When a metric is added later that may be missing
    #: from older results files, prefix its format spec with ``?:`` so
    #: OutputFormat filters the column out of tables that contain only
    #: pre-metric rows.
    REPORT_HEADINGS = {
        **COMMON_REPORT_HEADINGS,
        "throughput": "THRPUT",
        "script_throughput": "SCRTHRU",
    }

    REPORT_FORMATS = {
        "default": {
            "description": "Throughput summary: identity columns plus "
            "submit, alloc, and throughput rates",
            "format": "{scheduler:<14.14+}  {real_exec:<4}  "
            "?:{tag:<8.8+}  "
            "{nodes:>5}  {njobs:>6}  "
            "{submit_rate:>7.0f}  {alloc_rate:>7.0f}  "
            "{throughput:>7.0f}",
        },
        "long": {
            "description": "All throughput config and metric columns",
            "format": "{time:<16.16}  {scheduler:<14.14+}  "
            "{real_exec:<4}  "
            "?:{tag:<8.8+}  ?:{watcher:<8.8+}  "
            "{nodes:>5}  {cores:>5}  {gpus:>4}  {njobs:>6}  "
            "{submit_rate:>7.0f}  {alloc_rate:>7.0f}  "
            "?:{ingest_rate:>7.0f}  {throughput:>7.0f}  "
            "{script_throughput:>7.0f}",
        },
    }

    #: Mock-exec duration string passed to the broker via
    #: ``system.exec.test.run_duration``. Minimal so the broker spends almost
    #: no time in "executing" state, exposing the scheduling/dispatch pipeline
    #: as the rate-limiting step.
    _MOCK_RUN_DURATION = "0.001s"

    def __init__(
        self,
        njobs=1000,
        slot_cores=1,
        slot_gpus=0,
        watcher_factory=None,
        real_exec=False,
    ):
        self.njobs = njobs
        self.slot_cores = slot_cores
        self.slot_gpus = slot_gpus
        self.watcher_factory = watcher_factory
        self.real_exec = real_exec

    @classmethod
    def from_args(cls, args, resources, watcher_factory):
        """Construct from parsed argv. Throughput is resource-agnostic
        (job count comes from --njobs, not from the resource shape)."""
        del resources
        return cls(
            njobs=args.njobs,
            slot_cores=args.slot_cores,
            slot_gpus=args.slot_gpus,
            watcher_factory=watcher_factory,
            real_exec=args.real_exec,
        )

    @classmethod
    def config_dict(cls, args):
        return {
            "njobs": args.njobs,
            "slot_cores": args.slot_cores,
            "slot_gpus": args.slot_gpus,
        }

    def _build_jobspec(self):
        """Construct the per-job spec for this benchmark's mode.

        Mock mode pins a short ``mock_run_duration`` to keep execution out of
        the critical path. Real mode runs the default ``true`` command —
        instant exit, so the measurement is still scheduler+exec overhead with
        no meaningful per-job work. Callers wanting longer real jobs would
        need a future ``--command`` flag.
        """
        if self.real_exec:
            return simple_jobspec(
                slot_cores=self.slot_cores,
                slot_gpus=self.slot_gpus,
                mock=False,
            )
        return simple_jobspec(
            slot_cores=self.slot_cores,
            slot_gpus=self.slot_gpus,
            mock=True,
            mock_run_duration=self._MOCK_RUN_DURATION,
        )

    def run(self, handle, emitter):
        """Run the benchmark against ``handle``; emit progress to ``emitter``.

        Args:
            handle: Open :class:`flux.Flux` handle pointing at an instance
                with resources and a scheduler available (either via
                fake-resources injection or a real allocation, depending on
                the CLI mode).
            emitter: :class:`flux.testing.events.TestEventEmitter`.

        Returns:
            dict of metric names to numeric values: ``njobs``,
            ``submit_rate``, ``alloc_rate``, ``throughput``,
            ``script_throughput``.
        """
        # Phase-completion timestamps captured client-side. add_bulk_event_cb
        # fires once when every successfully-submitted job has seen the named
        # event AND every submit RPC has responded (see BulkRun docs). For
        # "submit" this is the moment the script knows all jobs are submitted.
        t_all_submitted = [None]
        t_all_alloc = [None]
        t_all_clean = [None]

        def on_all_submitted(_bulk):
            t_all_submitted[0] = time.time()
            # Phase boundary: all jobs are now in the broker. Hand off to the
            # execute stage so the UI's progress bar restarts from zero and
            # tracks clean events.
            emitter.stage("execute", 1, 2)
            # If clean events fired during the submit phase (mock-exec is fast
            # and the broker ingest batcher can be slow at scale, so some jobs
            # can run to completion before every submit is ack'd), surface
            # their accumulated count now. The clean tracker's gate suppressed
            # emission while we were in the submit stage; the metrics in the
            # result dict are unaffected by any of this because they're
            # computed from broker event timestamps, not UI progress.
            clean_tracker.flush(emitter)

        def on_all_alloc(_bulk):
            t_all_alloc[0] = time.time()

        def on_all_clean(_bulk):
            t_all_clean[0] = time.time()

        # Two-phase progress: submit events drive the submit bar
        # unconditionally; clean events drive the execute bar only once
        # on_all_submitted has flipped the gate open.
        submit_tracker = Tracker(self.njobs, "job")
        clean_tracker = Tracker(
            self.njobs,
            "job",
            gate=lambda: t_all_submitted[0] is not None,
        )

        def on_event(_bulk, _jobid, ev):
            if ev.name == "submit":
                submit_tracker.step(emitter)
            elif ev.name == "clean":
                clean_tracker.step(emitter)

        emitter.stage("submit", 0, 2)
        bulk_kwargs = {
            "events_of_interest": ("submit", "alloc", "exception", "clean"),
        }
        if self.watcher_factory is not None:
            bulk_kwargs["watcher_factory"] = self.watcher_factory
        bulk = BulkRun(handle, **bulk_kwargs)
        bulk.push_jobs(self._build_jobspec(), self.njobs)
        bulk.add_event_cb(on_event)
        bulk.add_bulk_event_cb("submit", on_all_submitted)
        bulk.add_bulk_event_cb("alloc", on_all_alloc)
        bulk.add_bulk_event_cb("clean", on_all_clean)

        # All script-side timestamps are time.time() (Unix epoch seconds) so
        # they live in the same domain as RFC 18 event timestamps and can be
        # subtracted directly.
        t_submit_start = time.time()
        result = bulk.run()
        t_done = time.time()

        # Sanity checks before the rate calculations crash with ValueErrors
        # from last_event_t() on a degenerate run. If no jobs successfully
        # submitted, every event lookup raises. If submits succeeded but no
        # job reached clean, the throughput formula has no upper endpoint.
        # Either way, surface the broker's reported causes (submit failure
        # strings, exception event notes) before failing.
        if result.njobs == 0:
            raise RuntimeError(
                failure_diagnostic(
                    result,
                    "throughput: all submits failed",
                )
            )
        if t_all_clean[0] is None:
            raise RuntimeError(
                failure_diagnostic(
                    result,
                    "throughput: no job reached 'clean' event",
                )
            )

        # Rate-denominator convention. See the module docstring at the top of
        # this file for the full rationale; the short version is: the headline
        # rates (submit_rate, alloc_rate) use script-observed endpoints, which
        # is what the user actually experienced. The broker-side equivalent
        # for submit is exposed separately as ingest_rate (it's a distinct,
        # useful measure — how fast can job-ingest accept submissions — but
        # it's not the answer to "how fast did I submit jobs").
        #
        # Left endpoint: always t_submit_start (script wall-clock at the start
        # of bulk.run()) so the rate captures every millisecond the user
        # waited, including ingest processing of the first job.
        #
        # Right endpoints: for headline rates, use t_all_X (script saw all
        # jobs reach phase X). For ingest_rate, use last_event_t("submit")
        # (broker emitted last submit event); the gap between the two captures
        # watcher-delivery overhead.
        submit_rate = result.njobs / (t_all_submitted[0] - t_submit_start)
        alloc_rate = result.njobs / (t_all_alloc[0] - t_submit_start)
        # ingest_rate: how fast job-ingest accepted submissions (broker-side,
        # no watcher latency). Pair with submit_rate to measure watcher
        # overhead.
        ingest_rate = result.njobs / (result.last_event_t("submit") - t_submit_start)
        # Throughput: full pipeline from the moment the script started
        # submitting to when the last job cleaned (broker-side right edge for
        # the headline number; the script-observed equivalent is
        # script_throughput below). This intentionally diverges from
        # src/test/throughput.py's definition (which uses
        # first_event_t("submit") on the left and so excludes the first job's
        # ingest processing time). Including ingest matches the user's actual
        # experience of the whole submission-to-completion duration.
        throughput = result.njobs / (result.last_event_t("clean") - t_submit_start)
        script_throughput = result.njobs / result.script_runtime

        return {
            "njobs": result.njobs,
            # Raw timestamps (Unix epoch seconds, wall-clock). Downstream
            # tooling can derive any rate from these.
            "t_submit_start": t_submit_start,
            "t_all_submitted": t_all_submitted[0],
            "t_first_submit_event": result.first_event_t("submit"),
            "t_last_submit_event": result.last_event_t("submit"),
            "t_all_alloc": t_all_alloc[0],
            "t_last_alloc_event": result.last_event_t("alloc"),
            "t_all_clean": t_all_clean[0],
            "t_last_clean_event": result.last_event_t("clean"),
            "t_done": t_done,
            # Derived rates (jobs/sec) for convenience.
            "submit_rate": submit_rate,
            "alloc_rate": alloc_rate,
            "ingest_rate": ingest_rate,
            "throughput": throughput,
            "script_throughput": script_throughput,
        }


class FillMachineBenchmark(Benchmark):
    """Saturate a resource set, then cancel-and-cleanup.

    Submits ``njobs`` jobs sized to ``slot_cores`` / ``slot_gpus`` that never
    finish on their own — mock mode uses ``mock_run_duration="0"`` (broker
    pretends infinite), real mode uses ``("sleep", "inf")``. When every job
    has reached ``start``, fires ``cancelall()``; when every job has reached
    ``clean``, stops the reactor.

    ``njobs`` is derived from the resource set, not the CLI ``--njobs``:
    :meth:`from_args` calls :func:`saturation_count` against the resolved
    resource shape so the benchmark's job count tracks whatever cluster
    (synthetic or real) it's running on.

    Reports:

    - ``time_to_fill``: seconds from the first ``submit`` event to
      the last ``start`` event (how long to fully populate the
      machine).
    - ``cancel_rate``: jobs cancelled per second, measured from
      cancel-issued (wall-clock) to the last ``clean`` event.
    - ``submit_rate``, ``alloc_rate``, ``start_rate``: per-event-type
      bookkeeping rates.

    Uses only the common ``flux schedbench run`` options
    (``--slot-cores``, ``--slot-gpus``; ``--njobs`` is ignored as the job
    count is resource-derived). :meth:`register_options` inherits the no-op
    default.
    """

    name = "fill-machine"
    stages = ("submit", "fill", "cancel")

    #: Headline is ``time_to_fill``; the other rates measure each
    #: phase (submit → ingest → alloc → start; cancel for teardown).
    SUMMARY_METRICS = (
        ("njobs", "jobs", "count"),
        ("time_to_fill", "fill time", "seconds"),
        ("submit_rate", "submit rate", "rate"),
        ("ingest_rate", "ingest rate", "rate"),
        ("alloc_rate", "alloc rate", "rate"),
        ("start_rate", "start rate", "rate"),
        ("cancel_rate", "cancel rate", "rate"),
    )

    RESULT = ("alloc_rate", "job/s")

    #: Report-only metadata; see ThroughputBenchmark for the pattern.
    #: Fill-machine's headline metric is ``time_to_fill`` plus the per-phase
    #: rates (start, cancel).
    REPORT_HEADINGS = {
        **COMMON_REPORT_HEADINGS,
        "start_rate": "START",
        "cancel_rate": "CANCEL",
        "time_to_fill": "TFILL",
    }

    REPORT_FORMATS = {
        "default": {
            "description": "Fill-machine summary: identity columns plus "
            "submit/start/cancel rates and time-to-fill",
            "format": "{scheduler:<14.14+}  {real_exec:<4}  "
            "?:{tag:<8.8+}  "
            "{nodes:>5}  {njobs:>6}  "
            "{submit_rate:>7.0f}  {start_rate:>7.0f}  "
            "{cancel_rate:>7.0f}  {time_to_fill:>7.2f}",
        },
        "long": {
            "description": "All fill-machine config and metric columns",
            "format": "{time:<16.16}  {scheduler:<14.14+}  "
            "{real_exec:<4}  "
            "?:{tag:<8.8+}  ?:{watcher:<8.8+}  "
            "{nodes:>5}  {cores:>5}  {gpus:>4}  {njobs:>6}  "
            "{submit_rate:>7.0f}  {alloc_rate:>7.0f}  "
            "?:{ingest_rate:>7.0f}  {start_rate:>7.0f}  "
            "{cancel_rate:>7.0f}  {time_to_fill:>7.2f}",
        },
    }

    #: Mock-exec duration "0" makes the broker pretend the job runs forever;
    #: cancel terminates it. Real-mode uses a ``sleep inf`` command instead —
    #: same semantics, real process. Both rely on cancellation rather than
    #: natural termination, so the cancel_rate metric measures the actual rate
    #: of broker-driven cleanup either way.
    _MOCK_RUN_DURATION = "0"
    _REAL_COMMAND = ("sleep", "inf")

    def __init__(
        self, njobs, slot_cores=1, slot_gpus=0, watcher_factory=None, real_exec=False
    ):
        self.njobs = njobs
        self.slot_cores = slot_cores
        self.slot_gpus = slot_gpus
        self.watcher_factory = watcher_factory
        self.real_exec = real_exec

    @classmethod
    def from_args(cls, args, resources, watcher_factory):
        """Construct from parsed argv. ``njobs`` is computed from the
        resource set via :func:`saturation_count` so the count tracks
        the cluster's actual shape, not the CLI default; the user's
        ``--njobs`` value is ignored."""
        njobs = saturation_count(
            resources["nodes"],
            resources["cores_per_node"],
            resources["gpus_per_node"],
            slot_cores=args.slot_cores,
            slot_gpus=args.slot_gpus,
        )
        return cls(
            njobs=njobs,
            slot_cores=args.slot_cores,
            slot_gpus=args.slot_gpus,
            watcher_factory=watcher_factory,
            real_exec=args.real_exec,
        )

    @classmethod
    def config_dict(cls, args):
        return {
            "slot_cores": args.slot_cores,
            "slot_gpus": args.slot_gpus,
            # njobs is derived from the resource set at run() time, not
            # taken from CLI. Downstream consumers should look at the
            # result's njobs field for the actual count.
        }

    def _build_jobspec(self):
        """Construct the per-job spec, selecting the
        cancel-driven shape for mock vs. real mode.

        Mock uses ``mock_run_duration="0"`` so the broker pretends the job is
        still running until the cancel event arrives. Real mode uses ``sleep
        inf`` for the same effect via an actual long-lived process.
        """
        if self.real_exec:
            return simple_jobspec(
                slot_cores=self.slot_cores,
                slot_gpus=self.slot_gpus,
                mock=False,
                command=self._REAL_COMMAND,
            )
        return simple_jobspec(
            slot_cores=self.slot_cores,
            slot_gpus=self.slot_gpus,
            mock=True,
            mock_run_duration=self._MOCK_RUN_DURATION,
        )

    def run(self, handle, emitter):
        """Run the benchmark; return aggregate metrics.

        Args:
            handle: Open :class:`flux.Flux` handle.
            emitter: :class:`flux.testing.events.TestEventEmitter`.

        Returns:
            dict with ``njobs``, ``time_to_fill``, ``submit_rate``,
            ``alloc_rate``, ``start_rate``, ``cancel_rate``.
        """
        njobs = self.njobs

        # Phase-completion timestamps captured client-side. All are
        # time.time() (Unix epoch seconds) so they live in the same domain as
        # RFC 18 event timestamps and can be subtracted directly.
        t_all_submitted = [None]
        t_all_alloc = [None]
        t_all_start = [None]
        t_cancel_issued = [None]
        t_all_clean = [None]

        # Three-phase progress: submit events drive the submit bar
        # unconditionally; start events drive the fill bar once
        # on_all_submitted opens its gate; clean events drive the cancel bar
        # once on_all_started opens its.
        submit_tracker = Tracker(njobs, "job")
        start_tracker = Tracker(
            njobs,
            "job",
            gate=lambda: t_all_submitted[0] is not None,
        )
        clean_tracker = Tracker(
            njobs,
            "cancel",
            gate=lambda: t_cancel_issued[0] is not None,
        )

        def on_event(_bulk, _jobid, ev):
            if ev.name == "submit":
                submit_tracker.step(emitter)
            elif ev.name == "start":
                start_tracker.step(emitter)
            elif ev.name == "clean":
                clean_tracker.step(emitter)

        def on_all_submitted(_bulk):
            t_all_submitted[0] = time.time()
            # Phase boundary: every job is in the broker. Hand off to the fill
            # stage so the UI bar restarts against start-event count. Surface
            # any start events that arrived during the submit phase so the
            # fill bar starts at the right value.
            emitter.stage("fill", 1, 3)
            start_tracker.flush(emitter)

        def on_all_alloc(_bulk):
            t_all_alloc[0] = time.time()

        def on_all_started(bulk):
            t_all_start[0] = time.time()
            # Stamp cancel-issued before the cancelall() RPC and the UI render
            # so the cancel_rate denominator reflects when cancellation was
            # actually requested, not when the progress bar finished painting.
            t_cancel_issued[0] = time.time()
            bulk.cancelall()
            emitter.stage("cancel", 2, 3)
            # Don't stop the reactor yet — wait for the resulting clean events
            # so we can measure the cancellation rate. If any clean events
            # arrived during the fill phase (would only happen on a job
            # failure since mock_run_duration="0" / "sleep inf" both forbid
            # natural completion), surface their count now so the cancel bar
            # starts at the right value rather than jumping ahead at the next
            # 100-event boundary.
            clean_tracker.flush(emitter)

        def on_all_clean(bulk):
            t_all_clean[0] = time.time()
            bulk.stop()

        bulk_kwargs = {
            "events_of_interest": ("submit", "alloc", "start", "exception", "clean"),
        }
        if self.watcher_factory is not None:
            bulk_kwargs["watcher_factory"] = self.watcher_factory
        bulk = BulkRun(handle, **bulk_kwargs)
        bulk.push_jobs(self._build_jobspec(), njobs)
        bulk.add_event_cb(on_event)
        bulk.add_bulk_event_cb("submit", on_all_submitted)
        bulk.add_bulk_event_cb("alloc", on_all_alloc)
        bulk.add_bulk_event_cb("start", on_all_started)
        bulk.add_bulk_event_cb("clean", on_all_clean)

        emitter.stage("submit", 0, 3)
        t_submit_start = time.time()
        result = bulk.run()
        t_done = time.time()

        # Sanity check: if on_all_started never fired, jobs went submit →
        # exception → clean without reaching the "start" event, bypassing the
        # cancel phase entirely. Surface a useful diagnostic before the rate
        # calculations crash with a deep ValueError from
        # result.last_event_t("start"). Both submit-RPC failures (OSError from
        # future.get_id()) and exception events (the broker's note explaining
        # why the job didn't run) are available on the BulkRunResult.
        if t_all_start[0] is None:
            raise RuntimeError(
                failure_diagnostic(
                    result,
                    "fill-machine: no job reached 'start' event",
                )
            )

        # time_to_fill: how long from when the script started submitting
        # (t_submit_start) until the last job reached "start" — i.e. the
        # machine was saturated. The right endpoint is broker-side because by
        # then every job has finished ingest and reached running state. See
        # the module docstring for why we use t_submit_start on the left
        # rather than first_event_t("submit").
        time_to_fill = result.last_event_t("start") - t_submit_start

        # Rate-denominator convention matches ThroughputBenchmark: headline
        # rates use script-observed endpoints, with ingest_rate exposed
        # separately as the broker-side submit measurement. See the module
        # docstring for the full rationale.
        submit_rate = njobs / (t_all_submitted[0] - t_submit_start)
        alloc_rate = njobs / (t_all_alloc[0] - t_submit_start)
        start_rate = njobs / (t_all_start[0] - t_submit_start)
        # ingest_rate: how fast job-ingest accepted submissions (broker-side,
        # no watcher latency). Pair with submit_rate to measure watcher
        # overhead.
        ingest_rate = njobs / (result.last_event_t("submit") - t_submit_start)
        # cancel_rate: from cancel-issued (script wall-clock) to the moment
        # the script saw every job clean. Both endpoints are script-side so
        # the rate matches what the user actually experienced during cleanup.
        cancel_rate = njobs / (t_all_clean[0] - t_cancel_issued[0])

        return {
            "njobs": njobs,
            # Raw timestamps (Unix epoch seconds, wall-clock). Downstream
            # tooling can derive any rate from these.
            "t_submit_start": t_submit_start,
            "t_all_submitted": t_all_submitted[0],
            "t_first_submit_event": result.first_event_t("submit"),
            "t_last_submit_event": result.last_event_t("submit"),
            "t_all_alloc": t_all_alloc[0],
            "t_last_alloc_event": result.last_event_t("alloc"),
            "t_all_start": t_all_start[0],
            "t_first_start_event": result.first_event_t("start"),
            "t_last_start_event": result.last_event_t("start"),
            "t_cancel_issued": t_cancel_issued[0],
            "t_all_clean": t_all_clean[0],
            "t_last_clean_event": result.last_event_t("clean"),
            "t_done": t_done,
            # Derived rates (jobs/sec) for convenience.
            "time_to_fill": time_to_fill,
            "submit_rate": submit_rate,
            "alloc_rate": alloc_rate,
            "start_rate": start_rate,
            "ingest_rate": ingest_rate,
            "cancel_rate": cancel_rate,
        }


# vi: ts=4 sw=4 expandtab
