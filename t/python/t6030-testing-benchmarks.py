#!/usr/bin/env python3
###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################
#
# flux.testing.schedbench tests
#
# Pure-Python tests for simple_jobspec(), plus broker-requiring
# integration tests for ThroughputBenchmark and FillMachineBenchmark
# against a real single-rank Flux instance.
#

import math
import os
import tempfile
import unittest

import flux
import flux.resource
from flux.job.stats import JobStats
from flux.testing.events import QUIET, TestEventEmitter
from flux.testing.fake_resources import (
    InjectFakeResources,
    saturation_count,
)
from flux.testing.job_watcher import (
    JournalEventWatcher,
    PerJobEventWatcher,
)
from flux.testing.schedbench import (
    BENCHMARKS,
    BenchmarkResults,
    FillMachineBenchmark,
    ThroughputBenchmark,
    simple_jobspec,
)
from flux.testing.schedbench.benchmarks import (
    Tracker,
    failure_diagnostic,
)
from subflux import rerun_under_flux


def __flux_size():
    return 1


class _BaseTestCase(unittest.TestCase):
    """Print the running test name to fd 2 at the start of each test.

    pycotap's TAPTestRunner captures sys.stderr per test and emits the
    contents as TAP diagnostics *after* the result line, which defeats the
    hang-debugging purpose: a test that hangs never reaches the point where
    the buffer is flushed, so its "Starting" line never appears. Writing
    directly to fd 2 with os.write bypasses pycotap's sys.stderr capture and
    shows the test name before the work starts.
    """

    def setUp(self):
        name = self.id().rpartition(".")[2]
        os.write(2, f"# Starting: {name}\n".encode())

    def _inject(self, **kw):
        """
        Install fake resources into the test broker, reloading the
        resource and sched-simple modules so they see the synthetic R.

        flux.testing.fake_resources.InjectFakeResources.install() only
        writes R to the KVS; the resource module reads R at startup and
        caches it, so a live mutation requires unloading and reloading.
        Production use goes through the fake-resources modprobe rc1 task
        (see flux-config-fake-resources(5)) which runs before resource
        loads; tests get to be pragmatic.
        """
        h = flux.Flux()
        for module in ("sched-simple", "resource"):
            try:
                h.rpc("module.remove", {"name": module}).get()
            except FileNotFoundError:
                pass
        fake = InjectFakeResources(**kw)
        fake.install(h)
        h.rpc(
            "module.load",
            {
                "path": "resource",
                "args": ["monitor-force-up", "noverify"],
                "exec": False,
            },
        ).get()
        h.rpc("module.load", {"path": "sched-simple", "args": [], "exec": False}).get()
        return h, fake


def _silent_emitter():
    """Return an emitter that drops everything below QUIET.

    Even QUIET still emits test.start / result / test.complete to stdout,
    which would interleave with TAP output. Use an explicit
    /dev/null-equivalent stream so the event channel goes nowhere.
    """
    return TestEventEmitter(verbosity=QUIET, stream=open("/dev/null", "w"))


class _StageRecorder:
    """Duck-typed emitter that records stage() and progress() calls.

    Used in the benchmark "run invariants" tests to verify the benchmark
    actually traversed every expected stage. Methods are intentionally minimal
    — benchmarks only call stage() and progress() on the emitter, so we record
    those and ignore the rest. If a benchmark grows new emitter call sites,
    this class will raise AttributeError, which is the desired feedback.
    """

    def __init__(self):
        self.stages = []
        self.progress_calls = []

    def stage(self, stage, stage_index, total_stages):
        self.stages.append(stage)

    def progress(self, current, total, unit, rate=None):
        self.progress_calls.append((current, total, unit))


class TestSimpleJobspec(unittest.TestCase):
    """simple_jobspec() builds JobspecV1 with the right attributes."""

    def test_default_includes_mock_execution(self):
        """Default sets system.exec.test.run_duration to the default"""
        spec = simple_jobspec()
        run_duration = spec.attributes["system"]["exec"]["test"]["run_duration"]
        self.assertEqual(run_duration, "0.001s")

    def test_mock_false_omits_mock_execution(self):
        """mock=False drops the mock-exec attribute entirely.

        This is the real-execution code path: the broker actually runs the
        jobspec's command rather than simulating it.
        """
        spec = simple_jobspec(mock=False)
        # The "system.exec.test" subtree should not exist when mock is False.
        # Either the key is absent or the path is incomplete.
        sys_attrs = spec.attributes.get("system", {})
        exec_attrs = sys_attrs.get("exec", {}).get("test", {})
        self.assertNotIn("run_duration", exec_attrs)

    def test_mock_run_duration_zero_for_infinite_jobs(self):
        """mock_run_duration='0' makes a never-finishing
        simulated job (used by fill-machine's mock mode)"""
        spec = simple_jobspec(mock_run_duration="0")
        run_duration = spec.attributes["system"]["exec"]["test"]["run_duration"]
        self.assertEqual(run_duration, "0")

    def test_mock_false_uses_supplied_command(self):
        """mock=False propagates command into the jobspec tasks
        (mock=True paths don't use the command, so the broker only honors it
        in real mode)."""
        spec = simple_jobspec(mock=False, command=("sleep", "inf"))
        tasks = spec.tasks
        self.assertEqual(tasks[0]["command"], ["sleep", "inf"])

    def test_slot_gpus_zero_omits_gpus_per_task(self):
        """slot_gpus=0 keeps gpus_per_task out of the request entirely"""
        spec = simple_jobspec(slot_cores=2, slot_gpus=0)
        # Walk the resource tree looking for a "gpu" node; should be absent.
        gpu_count = self._count_resource(spec.resources, "gpu")
        self.assertEqual(gpu_count, 0)

    def test_slot_gpus_positive_adds_gpus_per_task(self):
        """slot_gpus>0 adds a gpu resource node to the request"""
        spec = simple_jobspec(slot_cores=2, slot_gpus=3)
        gpu_count = self._count_resource(spec.resources, "gpu")
        self.assertEqual(gpu_count, 3)

    def test_slot_cores_passes_through(self):
        """slot_cores translates to cores_per_task"""
        spec = simple_jobspec(slot_cores=4)
        core_count = self._count_resource(spec.resources, "core")
        self.assertEqual(core_count, 4)

    def test_custom_command_passes_through(self):
        """command tuple is passed to JobspecV1 as the task argv"""
        spec = simple_jobspec(command=("/bin/echo", "hello"))
        argv = spec.tasks[0]["command"]
        self.assertEqual(argv, ["/bin/echo", "hello"])

    def _count_resource(self, resources, resource_type):
        """Walk a JobspecV1 resource tree and sum count for a type."""
        total = 0
        for node in resources or []:
            if node.get("type") == resource_type:
                total += node.get("count", 0)
            total += self._count_resource(
                node.get("with"),
                resource_type,
            )
        return total


class TestTracker(unittest.TestCase):
    """Unit tests for the internal Tracker helper that drives
    per-event progress emission in both benchmarks. Pure Python (no broker
    required) — the tracker only depends on having an object with a
    ``progress(current, total, label)`` method."""

    class _Emitter:
        """Records every progress() call as a tuple."""

        def __init__(self):
            self.events = []

        def progress(self, current, total, label, rate=None):
            del rate
            self.events.append((current, total, label))

    def test_emits_at_interval_and_final(self):
        """Plain tracker emits every 100 steps plus on the final
        step (for totals not divisible by 100)."""
        e = self._Emitter()
        t = Tracker(250, "job")
        for _ in range(250):
            t.step(e)
        self.assertEqual(
            e.events,
            [(100, 250, "job"), (200, 250, "job"), (250, 250, "job")],
        )

    def test_no_emit_when_count_below_interval(self):
        """Total smaller than the interval should still emit on
        the final step, even though no interval boundary is hit."""
        e = self._Emitter()
        t = Tracker(50, "job")
        for _ in range(50):
            t.step(e)
        self.assertEqual(e.events, [(50, 50, "job")])

    def test_gate_suppresses_emit_but_counts(self):
        """A closed gate counts steps but suppresses emission;
        opening it doesn't retroactively emit — call flush() for that.
        Subsequent steps with the gate open emit normally."""
        e = self._Emitter()
        open_gate = [False]
        t = Tracker(250, "job", gate=lambda: open_gate[0])
        for _ in range(150):
            t.step(e)
        self.assertEqual(t.count, 150)
        self.assertEqual(e.events, [])
        open_gate[0] = True
        # Opening the gate alone doesn't emit.
        self.assertEqual(e.events, [])
        # The next step that hits an interval boundary emits.
        for _ in range(50):
            t.step(e)
        self.assertEqual(e.events, [(200, 250, "job")])

    def test_flush_emits_current_count(self):
        """flush() emits the current count once, without
        stepping — used at phase boundaries to surface state accumulated while
        the gate was closed."""
        e = self._Emitter()
        t = Tracker(250, "job")
        # Step the counter to 150 by stepping under a closed gate so no events
        # fire yet.
        open_gate = [False]
        t._gate = lambda: open_gate[0]
        for _ in range(150):
            t.step(e)
        self.assertEqual(e.events, [])
        # Flush emits the accumulated count.
        open_gate[0] = True
        t.flush(e)
        self.assertEqual(e.events, [(150, 250, "job")])
        # The counter is unchanged by flush.
        self.assertEqual(t.count, 150)

    def test_flush_with_zero_count_is_noop(self):
        """flush() on a fresh tracker (count=0) emits nothing.
        Used by ThroughputBenchmark.on_all_submitted: in the common case no
        clean events fired during submit phase and the flush should be
        silent."""
        e = self._Emitter()
        t = Tracker(100, "job")
        t.flush(e)
        self.assertEqual(e.events, [])

    def test_label_passes_through(self):
        """The label argument is forwarded verbatim — used to
        distinguish 'job' progress from 'cancel' progress in fill-machine."""
        e = self._Emitter()
        t = Tracker(100, "cancel")
        for _ in range(100):
            t.step(e)
        self.assertEqual(e.events, [(100, 100, "cancel")])


class TestBenchmarkMetadata(unittest.TestCase):
    """Class-level metadata exposed for CLI lookup."""

    def test_throughput_name(self):
        """ThroughputBenchmark.name is the CLI-facing identifier"""
        self.assertEqual(ThroughputBenchmark.name, "throughput")

    def test_throughput_stages(self):
        """ThroughputBenchmark advertises 'submit' then 'execute'"""
        self.assertEqual(
            ThroughputBenchmark.stages,
            ("submit", "execute"),
        )

    def test_fill_machine_name(self):
        """FillMachineBenchmark.name is the CLI-facing identifier"""
        self.assertEqual(FillMachineBenchmark.name, "fill-machine")

    def test_fill_machine_stages(self):
        """FillMachineBenchmark advertises submit, fill, cancel"""
        self.assertEqual(
            FillMachineBenchmark.stages,
            ("submit", "fill", "cancel"),
        )

    def test_benchmarks_registry_has_built_ins(self):
        """BENCHMARKS maps each benchmark name to its class.

        Not asserting an exact length here because locality (and any
        future benchmark module imported by the package's __init__)
        also auto-registers — the registry is open-ended by design.
        """
        self.assertIs(BENCHMARKS["throughput"], ThroughputBenchmark)
        self.assertIs(BENCHMARKS["fill-machine"], FillMachineBenchmark)


class TestReportMetadata(unittest.TestCase):
    """Per-benchmark ``REPORT_HEADINGS`` and ``REPORT_FORMATS``
    metadata used by ``flux schedbench report``. The class-level metadata is
    what makes per-test reporting work — these tests exist to catch drift when
    REPORT_HEADINGS or REPORT_FORMATS are edited (a typo in either would
    otherwise silently misformat or break at runtime). For each benchmark
    class we check that:

    1. Every field referenced in every REPORT_FORMATS entry is declared in
    REPORT_HEADINGS (catches typos in format strings, removed-field drift). 2.
    REPORT_HEADINGS contains all the identity columns from
    COMMON_REPORT_HEADINGS (catches accidental shadowing in per-benchmark
    dicts). 3. A ``default`` format exists (the CLI default). 4. Each
    REPORT_FORMATS entry has both ``description`` and ``format`` keys (the
    shape UtilConfig expects).
    """

    #: Every registered benchmark gets these checks. Auto-derived
    #: from the registry so new benchmarks (built-in or plugin) are
    #: covered without test edits.
    BENCH_CLASSES = tuple(BENCHMARKS.values())

    def _format_fields(self, fmt_str):
        """Extract field names from a format string the same way
        ``OutputFormat`` does. Returns a list (not a set) so we can assert on
        the order the fields appear."""
        from string import Formatter

        names = []
        for _, field_name, _, _ in Formatter().parse(fmt_str):
            if field_name:
                # Strip any "!conv" suffix and ".attr" lookups; schedbench
                # formats don't use either today, but this matches
                # OutputFormat's behavior.
                base = field_name.split(".")[0].split("!")[0]
                names.append(base)
        return names

    def test_every_format_field_in_headings(self):
        """Every {field} in every REPORT_FORMATS entry must
        appear in the benchmark's REPORT_HEADINGS. This is the
        drift-prevention test: editing REPORT_FORMATS to use a field name that
        doesn't exist in headings would cause OutputFormat to raise
        ValueError("Unknown format field") at runtime, breaking ``flux
        schedbench report``."""
        for cls in self.BENCH_CLASSES:
            for fmt_name, fmt_spec in cls.REPORT_FORMATS.items():
                fields = self._format_fields(fmt_spec["format"])
                unknown = [f for f in fields if f not in cls.REPORT_HEADINGS]
                self.assertEqual(
                    unknown,
                    [],
                    msg=(
                        f"{cls.__name__}.REPORT_FORMATS[{fmt_name!r}] "
                        f"references unknown fields: {unknown}"
                    ),
                )

    def test_common_headings_inherited(self):
        """REPORT_HEADINGS must include every key from the
        common heading dict so identity columns (time, scheduler, nodes, ...)
        are spelled consistently across benchmarks."""
        from flux.testing.schedbench.benchmarks import (
            COMMON_REPORT_HEADINGS,
        )

        for cls in self.BENCH_CLASSES:
            for key, value in COMMON_REPORT_HEADINGS.items():
                self.assertIn(
                    key,
                    cls.REPORT_HEADINGS,
                    msg=f"{cls.__name__} missing common key {key!r}",
                )
                self.assertEqual(
                    cls.REPORT_HEADINGS[key],
                    value,
                    msg=(
                        f"{cls.__name__}.REPORT_HEADINGS[{key!r}] "
                        f"shadows common label: "
                        f"{cls.REPORT_HEADINGS[key]!r} vs {value!r}"
                    ),
                )

    def test_default_format_present(self):
        """Each benchmark must define a 'default' format because
        that's the argparse fallback when -o is unset."""
        for cls in self.BENCH_CLASSES:
            self.assertIn(
                "default",
                cls.REPORT_FORMATS,
                msg=f"{cls.__name__} missing 'default' format",
            )

    def test_format_entries_have_required_keys(self):
        """Each REPORT_FORMATS entry must be a dict with
        ``description`` and ``format`` keys (the shape
        ``UtilConfig.validate_formats`` expects)."""
        for cls in self.BENCH_CLASSES:
            for fmt_name, fmt_spec in cls.REPORT_FORMATS.items():
                self.assertIsInstance(
                    fmt_spec,
                    dict,
                    msg=f"{cls.__name__}.{fmt_name} is not a dict",
                )
                self.assertIn("description", fmt_spec)
                self.assertIn("format", fmt_spec)
                self.assertIsInstance(fmt_spec["format"], str)
                self.assertIsInstance(fmt_spec["description"], str)


class TestThroughputBenchmark(_BaseTestCase):
    """ThroughputBenchmark runs against a real broker with fake R."""

    def test_throughput_basic_run(self):
        """50-job run produces all four positive rate metrics"""
        h, fake = self._inject(nodes=4, cores_per_node=4)
        m = ThroughputBenchmark(njobs=50).run(h, _silent_emitter())

        self.assertEqual(m["njobs"], 50)
        self.assertGreater(m["submit_rate"], 0)
        self.assertGreater(m["alloc_rate"], 0)
        self.assertGreater(m["throughput"], 0)
        self.assertGreater(m["script_throughput"], 0)

    def test_throughput_with_gpus(self):
        """slot_gpus>0 still completes when fake R has GPUs"""
        h, fake = self._inject(
            nodes=4,
            cores_per_node=4,
            gpus_per_node=2,
        )
        m = ThroughputBenchmark(
            njobs=20,
            slot_cores=1,
            slot_gpus=1,
        ).run(h, _silent_emitter())

        self.assertEqual(m["njobs"], 20)
        self.assertGreater(m["throughput"], 0)

    def test_throughput_njobs_matches_input(self):
        """The njobs result equals what was submitted"""
        h, fake = self._inject(nodes=4, cores_per_node=4)
        m = ThroughputBenchmark(njobs=25).run(h, _silent_emitter())
        self.assertEqual(m["njobs"], 25)

    def test_throughput_watcher_swap_equivalent_results(self):
        """Journal and per-job watchers both produce a complete,
        well-ordered metric set with positive rates and the same njobs. They
        differ in script-side timing (watcher overhead), but every invariant
        the benchmark guarantees must hold for either choice. Catches a
        watcher-factory threading bug — if watcher_factory weren't being
        passed through to BulkRun, one of these would fail or hang."""
        for name, factory in (
            ("journal", lambda h: JournalEventWatcher(h)),
            ("per-job", lambda h: PerJobEventWatcher(h)),
        ):
            with self.subTest(watcher=name):
                h, fake = self._inject(nodes=4, cores_per_node=4)
                m = ThroughputBenchmark(
                    njobs=20,
                    watcher_factory=factory,
                ).run(h, _silent_emitter())

                self.assertEqual(m["njobs"], 20)
                # All headline rates must be finite and positive. ingest_rate
                # is the broker-side submit measure; submit_rate is the
                # script-observed counterpart.
                for key in (
                    "submit_rate",
                    "alloc_rate",
                    "ingest_rate",
                    "throughput",
                    "script_throughput",
                ):
                    self.assertTrue(
                        math.isfinite(m[key]),
                        "{0}/{1} = {2}".format(name, key, m[key]),
                    )
                    self.assertGreater(
                        m[key],
                        0,
                        "{0}/{1} = {2}".format(name, key, m[key]),
                    )

                # Script-observed submit_rate cannot exceed the broker-side
                # ingest_rate within the same run: the script's bulk callback
                # fires only after the watcher has delivered every submit
                # event, so the script's window is no shorter than the
                # broker's.
                self.assertLessEqual(
                    m["submit_rate"],
                    m["ingest_rate"] * 1.001,
                )

    def test_throughput_run_invariants(self):
        """Stages emitted, jobs submitted+completed, metrics
        finite/positive, timestamps monotonically ordered, resources released"""
        h, fake = self._inject(nodes=4, cores_per_node=4)

        # Snapshot job counts before the benchmark so we can isolate this
        # run's submissions from any from prior tests (the broker accumulates
        # inactive jobs across the whole subflux session).
        before = JobStats(h).update_sync()

        rec = _StageRecorder()
        m = ThroughputBenchmark(njobs=20).run(h, rec)

        after = JobStats(h).update_sync()

        # The benchmark advertises two stages ("submit" then "execute") and
        # must actually traverse both; otherwise on_event never wired up or
        # on_all_submitted didn't fire.
        self.assertEqual(rec.stages, ["submit", "execute"])

        # Exactly 20 new jobs reached the broker and all transitioned to
        # inactive. Catches "njobs was reported correctly but fewer jobs
        # actually got submitted" silently.
        self.assertEqual(after.total - before.total, 20)
        self.assertEqual(after.inactive - before.inactive, 20)
        # Mock-exec jobs return 0, so all 20 should be counted as successful —
        # not failed/canceled/timeout.
        self.assertEqual(after.successful - before.successful, 20)
        self.assertEqual(after.failed - before.failed, 0)

        # Every rate metric must be finite and positive — catches inf/nan from
        # divide-by-zero on degenerate event spans.
        for key in (
            "submit_rate",
            "alloc_rate",
            "ingest_rate",
            "throughput",
            "script_throughput",
        ):
            self.assertTrue(math.isfinite(m[key]), "{0} = {1}".format(key, m[key]))
            self.assertGreater(m[key], 0, "{0} = {1}".format(key, m[key]))

        # Watcher overhead invariant: submit_rate is the script-observed rate
        # (njobs / (t_all_submitted - t_submit_start)); ingest_rate is the
        # broker-side counterpart (njobs / (t_last_submit_event -
        # t_submit_start)). The script's bulk callback fires after the
        # broker's last event is delivered, so the script's denominator is at
        # least as long, hence submit_rate <= ingest_rate. A violation would
        # mean the bulk callback and broker event timestamps are out of order.
        self.assertLessEqual(
            m["submit_rate"],
            m["ingest_rate"] * 1.001,
        )

        # The full timestamp set is present. Downstream tools rely on the
        # exact key names, so check them explicitly.
        for key in (
            "t_submit_start",
            "t_all_submitted",
            "t_first_submit_event",
            "t_last_submit_event",
            "t_all_alloc",
            "t_last_alloc_event",
            "t_all_clean",
            "t_last_clean_event",
            "t_done",
        ):
            self.assertIn(key, m)
            self.assertTrue(
                math.isfinite(m[key]),
                "{0} = {1}".format(key, m[key]),
            )

        # Timestamp orderings, split into chains the implementation actually
        # guarantees. A single monotonic chain across script and broker times
        # isn't valid: flux pipelines events, so a broker-side first-Y event
        # can precede a script-side all-X bulk-callback firing.

        # Script-side callback ordering (BulkRun fires bulk callbacks
        # sequentially in lifecycle order; bulk.run returns after
        # on_all_clean):
        self.assertLessEqual(m["t_submit_start"], m["t_all_submitted"])
        self.assertLessEqual(m["t_all_submitted"], m["t_all_alloc"])
        self.assertLessEqual(m["t_all_alloc"], m["t_all_clean"])
        self.assertLessEqual(m["t_all_clean"], m["t_done"])

        # Within-event-type broker ordering:
        self.assertLessEqual(
            m["t_first_submit_event"],
            m["t_last_submit_event"],
        )

        # Cross-event-type broker ordering by per-job causality: the job that
        # has last_X also has X before its own Y, and that Y ≤ last_Y over all
        # jobs.
        self.assertLessEqual(
            m["t_last_submit_event"],
            m["t_last_alloc_event"],
        )
        self.assertLessEqual(
            m["t_last_alloc_event"],
            m["t_last_clean_event"],
        )

        # Watcher delivery latency: broker emits each event before the
        # script's matching bulk callback fires.
        self.assertLessEqual(
            m["t_last_submit_event"],
            m["t_all_submitted"],
        )
        self.assertLessEqual(
            m["t_last_alloc_event"],
            m["t_all_alloc"],
        )
        self.assertLessEqual(
            m["t_last_clean_event"],
            m["t_all_clean"],
        )

        # Submit starts in the script before the broker sees any submit event.
        self.assertLessEqual(
            m["t_submit_start"],
            m["t_first_submit_event"],
        )

        # Both throughput numerators are njobs. Denominators: throughput uses
        # last_clean_event - t_submit_start (broker-side right edge,
        # script-side left edge). script_runtime measures from BulkRun
        # construction to bulk.run() return, which always wraps the broker
        # event window (last_clean_event is delivered to the script before
        # bulk.run() returns). So script_runtime >= the throughput
        # denominator, hence script_throughput <= throughput. 1% slop for
        # floats.
        self.assertLessEqual(
            m["script_throughput"],
            m["throughput"] * 1.01,
        )

        # All resources should be free again: jobs completed and the scheduler
        # released their slots.
        rl = flux.resource.resource_list(h).get()
        self.assertEqual(rl.allocated.nnodes, 0)


class TestFillMachineBenchmark(_BaseTestCase):
    """FillMachineBenchmark saturates fake R, cancels, measures."""

    def test_fill_machine_cpu_and_gpu(self):
        """4x4x2 with 1c/1g slot saturates at 8 jobs"""
        h, fake = self._inject(
            nodes=4,
            cores_per_node=4,
            gpus_per_node=2,
        )
        njobs = saturation_count(
            fake.nodes,
            fake.cores_per_node,
            fake.gpus_per_node,
            slot_cores=1,
            slot_gpus=1,
        )
        m = FillMachineBenchmark(
            njobs=njobs,
            slot_cores=1,
            slot_gpus=1,
        ).run(h, _silent_emitter())

        self.assertEqual(m["njobs"], 8)
        self.assertGreater(m["time_to_fill"], 0)
        self.assertGreater(m["cancel_rate"], 0)
        self.assertGreater(m["submit_rate"], 0)
        self.assertGreater(m["alloc_rate"], 0)
        self.assertGreater(m["start_rate"], 0)

    def test_fill_machine_cpu_only(self):
        """4x4 with 1c slot saturates at 16 jobs"""
        h, fake = self._inject(nodes=4, cores_per_node=4)
        njobs = saturation_count(
            fake.nodes,
            fake.cores_per_node,
            fake.gpus_per_node,
            slot_cores=1,
        )
        m = FillMachineBenchmark(
            njobs=njobs,
            slot_cores=1,
        ).run(h, _silent_emitter())

        self.assertEqual(m["njobs"], 16)
        self.assertGreater(m["time_to_fill"], 0)
        self.assertGreater(m["cancel_rate"], 0)

    def test_fill_machine_wider_slot(self):
        """Slot wider than 1 core reduces saturation count"""
        h, fake = self._inject(nodes=4, cores_per_node=4)
        njobs = saturation_count(
            fake.nodes,
            fake.cores_per_node,
            fake.gpus_per_node,
            slot_cores=2,
        )
        m = FillMachineBenchmark(
            njobs=njobs,
            slot_cores=2,
        ).run(h, _silent_emitter())

        # 4 nodes x (4 cores / 2 cores per slot) = 8 jobs
        self.assertEqual(m["njobs"], 8)

    def test_fill_machine_njobs_matches_saturation_count(self):
        """The benchmark submits exactly the njobs it was given,
        which is what callers — including flux-schedbench — get from
        saturation_count on the resource shape. This is the end-to-end check
        that the public saturation_count helper and FillMachineBenchmark's
        njobs argument compose to produce a fully-packed broker."""
        h, fake = self._inject(
            nodes=4,
            cores_per_node=4,
            gpus_per_node=2,
        )
        expected = saturation_count(
            fake.nodes,
            fake.cores_per_node,
            fake.gpus_per_node,
            slot_cores=1,
            slot_gpus=1,
        )
        bench = FillMachineBenchmark(
            njobs=expected,
            slot_cores=1,
            slot_gpus=1,
        )
        m = bench.run(h, _silent_emitter())
        self.assertEqual(m["njobs"], expected)

    def test_fill_machine_run_invariants(self):
        """All three stages emitted, jobs canceled, metrics
        finite/positive, timestamps monotonically ordered, resources
        released"""
        h, fake = self._inject(nodes=4, cores_per_node=4)

        before = JobStats(h).update_sync()

        njobs = saturation_count(
            fake.nodes,
            fake.cores_per_node,
            fake.gpus_per_node,
            slot_cores=1,
        )
        rec = _StageRecorder()
        m = FillMachineBenchmark(
            njobs=njobs,
            slot_cores=1,
        ).run(h, rec)

        after = JobStats(h).update_sync()

        # The benchmark must traverse submit → fill → cancel in order. If
        # "fill" is missing, on_all_submitted never fired. If "cancel" is
        # missing, on_all_started never fired (i.e., not all jobs reached
        # "start"). Either gap indicates a real bug.
        self.assertEqual(rec.stages, ["submit", "fill", "cancel"])

        njobs = m["njobs"]
        # All njobs reached the broker and transitioned to inactive.
        self.assertEqual(after.total - before.total, njobs)
        self.assertEqual(after.inactive - before.inactive, njobs)
        # Every job was canceled (not completed naturally — they had
        # mock_run_duration="0" so they only exit via cancel). JobStats counts
        # canceled jobs under "failed" as well.
        self.assertEqual(after.canceled - before.canceled, njobs)
        self.assertEqual(after.successful - before.successful, 0)

        # Every rate metric must be finite and positive.
        for key in (
            "time_to_fill",
            "submit_rate",
            "alloc_rate",
            "start_rate",
            "ingest_rate",
            "cancel_rate",
        ):
            self.assertTrue(math.isfinite(m[key]), "{0} = {1}".format(key, m[key]))
            self.assertGreater(m[key], 0, "{0} = {1}".format(key, m[key]))

        # Watcher overhead invariant: submit_rate (script-observed) is bounded
        # above by ingest_rate (broker-side). See ThroughputBenchmark for the
        # derivation.
        self.assertLessEqual(
            m["submit_rate"],
            m["ingest_rate"] * 1.001,
        )

        # The full timestamp set is present. Downstream tools rely on the
        # exact key names, so check them explicitly.
        for key in (
            "t_submit_start",
            "t_all_submitted",
            "t_first_submit_event",
            "t_last_submit_event",
            "t_all_alloc",
            "t_last_alloc_event",
            "t_all_start",
            "t_first_start_event",
            "t_last_start_event",
            "t_cancel_issued",
            "t_all_clean",
            "t_last_clean_event",
            "t_done",
        ):
            self.assertIn(key, m)
            self.assertTrue(
                math.isfinite(m[key]),
                "{0} = {1}".format(key, m[key]),
            )

        # Timestamp orderings. The previous version of this test asserted a
        # single 12-element monotonic chain spanning script-side and
        # broker-side timestamps, but flux pipelines events: by the time
        # BulkRun's on_all_alloc fires (script-side, last job's alloc
        # delivered), the broker has often already emitted the first start
        # event for an earlier job. Pipelining breaks "all-of-phase-X before
        # any-of-phase-Y" assumptions whenever they cross the script/broker
        # boundary. The split below captures only what the implementation
        # actually guarantees.

        # (1) Script-side callback ordering. BulkRun fires the bulk callbacks
        # sequentially in lifecycle order; cancel is issued from inside
        # on_all_started; bulk.run() returns after on_all_clean.
        self.assertLessEqual(m["t_submit_start"], m["t_all_submitted"])
        self.assertLessEqual(m["t_all_submitted"], m["t_all_alloc"])
        self.assertLessEqual(m["t_all_alloc"], m["t_all_start"])
        self.assertLessEqual(m["t_all_start"], m["t_cancel_issued"])
        self.assertLessEqual(m["t_cancel_issued"], m["t_all_clean"])
        self.assertLessEqual(m["t_all_clean"], m["t_done"])

        # (2) Within-event-type broker ordering: first ≤ last.
        self.assertLessEqual(
            m["t_first_submit_event"],
            m["t_last_submit_event"],
        )
        self.assertLessEqual(
            m["t_first_start_event"],
            m["t_last_start_event"],
        )

        # (3) Cross-event-type broker ordering by per-job causality. For each
        # job, submit < alloc < start < clean. The job that has the latest X
        # event had its X event before its own Y event (Y after X in
        # lifecycle), and its Y event ≤ the latest Y event across all jobs. So
        # last_X ≤ last_Y. This does NOT extend to first_X vs last_Y across
        # phases.
        self.assertLessEqual(
            m["t_last_submit_event"],
            m["t_last_alloc_event"],
        )
        self.assertLessEqual(
            m["t_last_alloc_event"],
            m["t_last_start_event"],
        )
        self.assertLessEqual(
            m["t_last_start_event"],
            m["t_last_clean_event"],
        )

        # (4) Watcher delivery latency: the broker emits each event before the
        # script's bulk callback for that event type fires (the watcher
        # delivers events into BulkRun, which then aggregates into the bulk
        # callback).
        self.assertLessEqual(
            m["t_last_submit_event"],
            m["t_all_submitted"],
        )
        self.assertLessEqual(
            m["t_last_alloc_event"],
            m["t_all_alloc"],
        )
        self.assertLessEqual(
            m["t_last_start_event"],
            m["t_all_start"],
        )
        self.assertLessEqual(
            m["t_last_clean_event"],
            m["t_all_clean"],
        )

        # (5) Submit starts in the script before the broker observes any
        # submit event.
        self.assertLessEqual(
            m["t_submit_start"],
            m["t_first_submit_event"],
        )

        # (6) Cancel-then-clean: cancel is issued from on_all_started, so it
        # precedes any subsequent clean event in the broker.
        self.assertLessEqual(
            m["t_cancel_issued"],
            m["t_last_clean_event"],
        )

        # After cancel-and-cleanup, all allocated resources should be released
        # back to free.
        rl = flux.resource.resource_list(h).get()
        self.assertEqual(rl.allocated.nnodes, 0)


class TestBenchmarkResults(unittest.TestCase):
    """BenchmarkResults round-trips and writes atomically."""

    def setUp(self):
        # Each test gets its own temp directory so files are isolated;
        # tempfile.TemporaryDirectory cleans up on tearDown.
        self._tmpdir = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmpdir.cleanup)
        self.path = os.path.join(self._tmpdir.name, "results.json")

    def test_load_missing_file_yields_empty(self):
        """Constructing against a nonexistent path gives an empty store"""
        results = BenchmarkResults(self.path)
        self.assertEqual(results.get_runs(), [])

    def test_add_run_appends_in_memory(self):
        """add_run grows the in-memory list but does not write"""
        results = BenchmarkResults(self.path)
        results.add_run({"test_name": "throughput"})
        self.assertEqual(len(results.get_runs()), 1)
        # File on disk should still not exist
        self.assertFalse(os.path.exists(self.path))

    def test_add_run_sets_timestamps(self):
        """add_run fills timestamp and iso_timestamp if absent"""
        results = BenchmarkResults(self.path)
        record = results.add_run({"test_name": "throughput"})
        self.assertIn("timestamp", record)
        self.assertIn("iso_timestamp", record)
        # iso_timestamp is YYYY-MM-DDTHH:MM:SSZ form
        self.assertRegex(
            record["iso_timestamp"],
            r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$",
        )

    def test_add_run_preserves_provided_timestamp(self):
        """A caller-supplied timestamp is not overwritten"""
        results = BenchmarkResults(self.path)
        record = results.add_run(
            {
                "test_name": "throughput",
                "timestamp": 1234567890.0,
            }
        )
        self.assertEqual(record["timestamp"], 1234567890.0)

    def test_save_and_reload_round_trip(self):
        """A run record survives save → load via a new instance"""
        a = BenchmarkResults(self.path)
        a.add_run(
            {
                "test_name": "throughput",
                "host": "testhost",
                "benchmarks": {
                    "throughput": {
                        "results": {"njobs": 100, "throughput": 250.5},
                    },
                },
            }
        )
        a.save()

        b = BenchmarkResults(self.path)
        runs = b.get_runs()
        self.assertEqual(len(runs), 1)
        self.assertEqual(runs[0]["test_name"], "throughput")
        self.assertEqual(runs[0]["host"], "testhost")
        self.assertEqual(
            runs[0]["benchmarks"]["throughput"]["results"]["throughput"],
            250.5,
        )

    def test_save_appends_across_instances(self):
        """A second instance preserves prior runs and adds more"""
        a = BenchmarkResults(self.path)
        a.add_run({"test_name": "throughput"})
        a.save()

        b = BenchmarkResults(self.path)
        b.add_run({"test_name": "fill-machine"})
        b.save()

        c = BenchmarkResults(self.path)
        names = [r["test_name"] for r in c.get_runs()]
        self.assertEqual(names, ["throughput", "fill-machine"])

    def test_save_atomic_no_tmp_leak(self):
        """After save(), the .tmp file is renamed away (not left behind)"""
        results = BenchmarkResults(self.path)
        results.add_run({"test_name": "throughput"})
        results.save()
        # The tmp file should have been renamed; only the final path remains.
        self.assertTrue(os.path.exists(self.path))
        self.assertFalse(os.path.exists(self.path + ".tmp"))

    def test_save_overwrites_atomically(self):
        """Re-saving over an existing file replaces it cleanly"""
        a = BenchmarkResults(self.path)
        a.add_run({"test_name": "throughput"})
        a.save()
        size_one_run = os.path.getsize(self.path)

        # Append another run and re-save; file should grow.
        a.add_run({"test_name": "fill-machine"})
        a.save()
        size_two_runs = os.path.getsize(self.path)
        self.assertGreater(size_two_runs, size_one_run)


class _FakeEvent:
    """Minimal stand-in for the event objects BulkRun records,
    just enough for failure_diagnostic to read context."""

    def __init__(self, context):
        self.name = "exception"
        self.timestamp = 1000.0
        self.context = context


class _FakeBulkRunResult:
    """Minimal stand-in for BulkRunResult: just the attributes
    failure_diagnostic reads."""

    def __init__(
        self,
        njobs,
        attempted,
        submit_failures,
        exception_jobs=None,
        alloc=0,
        start=0,
        clean=0,
    ):
        self.njobs = njobs
        self.submit_attempted = attempted
        self.submit_failures = submit_failures
        self.jobs = {}
        for jid, ctx in (exception_jobs or {}).items():
            self.jobs[jid] = {"exception": _FakeEvent(ctx)}
        self._counts = {"alloc": alloc, "start": start, "clean": clean}

    def jobids_with(self, name):
        if name == "exception":
            return [jid for jid in self.jobs if "exception" in self.jobs[jid]]
        return list(range(self._counts.get(name, 0)))


class TestFailureDiagnostic(unittest.TestCase):
    """failure_diagnostic groups identical errors into idsets
    so a 1000-jobs-all-failed scenario renders as one line."""

    def test_uniform_failures_collapse_to_idset(self):
        """1024 submissions all with the same error → one line."""
        r = _FakeBulkRunResult(
            njobs=0,
            attempted=1024,
            submit_failures=[
                {"batch": 0, "submit_index": i, "error": "rejected"}
                for i in range(1024)
            ],
        )
        msg = failure_diagnostic(r, "throughput: all submits failed")
        # The 1024 submission indices collapse into a single "0-1023:" prefix
        # on a single line.
        self.assertIn("0-1023: rejected", msg)
        self.assertEqual(msg.count("rejected"), 1)

    def test_mixed_failures_grouped_separately(self):
        """Two distinct errors with shuffled arrival order are
        both reported, each with a correct idset."""
        fails = []
        for i in range(800):
            fails.append({"batch": 0, "submit_index": i, "error": "error A"})
        for i in range(800, 1000):
            fails.append({"batch": 0, "submit_index": i, "error": "error B"})
        # Shuffle to simulate out-of-order arrival.
        import random

        random.Random(42).shuffle(fails)

        r = _FakeBulkRunResult(
            njobs=0,
            attempted=1000,
            submit_failures=fails,
        )
        msg = failure_diagnostic(r, "throughput: all submits failed")
        self.assertIn("0-799: error A", msg)
        self.assertIn("800-999: error B", msg)

    def test_most_common_error_listed_first(self):
        """Groups are ordered by failure count descending."""
        fails = [{"batch": 0, "submit_index": i, "error": "rare"} for i in range(2)] + [
            {"batch": 0, "submit_index": i + 2, "error": "common"} for i in range(100)
        ]
        r = _FakeBulkRunResult(
            njobs=0,
            attempted=102,
            submit_failures=fails,
        )
        msg = failure_diagnostic(r, "throughput: all submits failed")
        common_pos = msg.find("common")
        rare_pos = msg.find("rare")
        self.assertGreater(common_pos, 0)
        self.assertGreater(rare_pos, common_pos)

    def test_exception_events_grouped_by_note(self):
        """Identical exception notes report once with a count."""
        exceptions = {}
        for jid in range(100, 1100):
            exceptions[jid] = {
                "type": "alloc",
                "severity": 0,
                "note": "unsatisfiable resource request",
            }
        r = _FakeBulkRunResult(
            njobs=1000,
            attempted=1000,
            submit_failures=[],
            exception_jobs=exceptions,
            alloc=0,
            start=0,
            clean=1000,
        )
        msg = failure_diagnostic(
            r,
            "fill-machine: no job reached 'start' event",
        )
        self.assertIn("1000 job(s)", msg)
        self.assertIn("'unsatisfiable resource request'", msg)
        # Exactly one summary line, not 1000 individual lines.
        self.assertEqual(msg.count("type=alloc"), 1)

    def test_pending_hint_when_no_errors_recorded(self):
        """If nothing reached an expected event AND no failures
        were recorded, the user gets pointed at `flux jobs -a`."""
        r = _FakeBulkRunResult(
            njobs=4,
            attempted=4,
            submit_failures=[],
            exception_jobs={},
        )
        msg = failure_diagnostic(
            r,
            "fill-machine: no job reached 'start' event",
        )
        self.assertIn("flux jobs -a", msg)

    def test_phase_counts_in_header_section(self):
        """The header section reports requested / submitted /
        failed plus counts at each lifecycle phase."""
        r = _FakeBulkRunResult(
            njobs=10,
            attempted=12,
            submit_failures=[
                {"batch": 0, "submit_index": 10, "error": "x"},
                {"batch": 0, "submit_index": 11, "error": "x"},
            ],
            alloc=10,
            start=0,
            clean=10,
        )
        msg = failure_diagnostic(
            r,
            "fill-machine: no job reached 'start' event",
        )
        self.assertIn("12 requested", msg)
        self.assertIn("10 submitted", msg)
        self.assertIn("2 submit-failed", msg)
        self.assertIn("10/10 reached event 'alloc'", msg)
        self.assertIn("0/10 reached event 'start'", msg)

    def test_backward_compat_no_submit_index(self):
        """submit_failures entries lacking 'submit_index' (e.g.,
        from older BulkRun versions) still render — just as a count rather
        than an idset."""
        r = _FakeBulkRunResult(
            njobs=0,
            attempted=3,
            submit_failures=[
                {"batch": 0, "error": "error A"},
                {"batch": 0, "error": "error A"},
                {"batch": 0, "error": "error B"},
            ],
        )
        msg = failure_diagnostic(r, "throughput: all submits failed")
        self.assertIn("2 submissions: error A", msg)
        self.assertIn("1 submissions: error B", msg)


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner(), buffer=False)

# vi: ts=4 sw=4 expandtab
