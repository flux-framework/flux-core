###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Locality benchmark for flux-schedbench.

Submits multi-slot jobs and scores how well the scheduler packs each
slot's cores and GPUs into a single locality domain. See
:class:`LocalityBenchmark` for CLI options and
:class:`LocalityPredicate` for the scoring rule.
"""

import logging
import random
import time
import xml.etree.ElementTree as ET
from collections import Counter

import flux.util
from flux.idset import IDset
from flux.job import JobspecV1
from flux.testing.bulkrun import BulkRun
from flux.testing.fake_resources import saturation_count
from flux.testing.schedbench.benchmarks import (
    COMMON_REPORT_HEADINGS,
    Benchmark,
    Tracker,
    failure_diagnostic,
)

LOGGER = logging.getLogger("flux-schedbench.locality")


#: hwloc OSDev name prefixes that indicate compute-capable GPU
#: backends. Used to distinguish compute GPUs from Linux DRM
#: display nodes (which appear as ``card*``, ``renderD*``,
#: ``controlD*`` under the same PCIDev) when the OSDev's
#: ``osdev_type`` alone is ambiguous.
_COMPUTE_GPU_PREFIXES = ("cuda", "opencl", "rsmi", "nvml", "nvidia")


def _is_compute_gpu(obj):
    """Return True if an OSDev element represents a compute-
    capable GPU.

    hwloc classifies GPUs inconsistently across backends:

    - CUDA / OpenCL devices arrive as ``osdev_type=5`` (CoProc).
    - RSMI / NVML / NVIDIA devices arrive as ``osdev_type=1``
      (GPU) with a backend-specific name.
    - Linux DRM display nodes also arrive as ``osdev_type=1``
      with names like ``card0`` / ``renderD128`` / ``controlD64``.

    The combined criterion is therefore: any CoProc, OR an
    osdev_type=1 device whose name starts with a known
    compute-backend prefix. This correctly catches all four
    compute backends while still excluding display-only DRM
    nodes.

    The flux C-level resource module performs the equivalent
    classification authoritatively; if a future hwloc backend
    appears that this Python list doesn't cover, the right
    long-term fix is to expose that classifier through the
    Python bindings rather than extending this list. For a
    testing-only module the list is sufficient.
    """
    osdev_type = obj.get("osdev_type")
    if osdev_type == "5":
        return True
    if osdev_type == "1":
        name = obj.get("name", "")
        return any(name.startswith(p) for p in _COMPUTE_GPU_PREFIXES)
    return False


def _parse_duration_mode(spec):
    """Parse --duration into ``(mode, lo, hi)``.

    Modes are ``"fixed"`` (single FSD; ``lo == hi == spec``),
    ``"fill"`` (never-completing; both None), or ``"random"``
    (uniform in [lo, hi], default range ``0.1s-1.0s``).
    """
    if spec == "fill":
        return ("fill", None, None)
    if spec == "random":
        return ("random", "0.1s", "1.0s")
    if spec.startswith("random:"):
        lo, _, hi = spec[len("random:") :].partition("-")
        if not (lo and hi):
            raise ValueError(f"--duration=random:LO-HI: malformed range in {spec!r}")
        return ("random", lo, hi)
    # Anything else parses as a fixed FSD; raises on bad input.
    flux.util.parse_fsd(spec)
    return ("fixed", spec, spec)


def _parse_idset_string(s):
    """Parse an idset like ``"0-7,16,20-23"`` into a list of ints."""
    return list(IDset(s)) if s else []


class LocalityPredicate:
    """Per-slot locality scorer driven by an hwloc XML topology.

    Indexes each core and CoProc GPU by its locality domain — by
    default the hwloc ``nodeset`` attribute (NUMA-node membership).
    :meth:`score_job` then evaluates a job's R: a slot is satisfied
    iff its cores and GPUs all share a domain. The fake-resources
    rc1 task replicates one node's topology across all ranks, so a
    single XML applies to every rank.

    Cores and GPUs are keyed by **sequential document-order index**
    (0, 1, 2, ...), mirroring how ``flux R encode`` and the
    :mod:`fluxion` JGF amender number resources. Keying by hwloc's
    ``os_index`` is not safe: on multi-socket AMD systems os_index
    collides between packages (the kernel uses per-socket physical
    CPU IDs), and the GPU OSDev's trailing-digit name does not
    necessarily match the scheduler's GPU index (e.g. AMD's
    ``opencl0d3`` is the fourth GPU in document order, but the
    digit is ``3`` only by coincidence).

    Override :meth:`_domain_of` to change the locality rule.
    Nodeset is the right default for modern hardware where NUMA is
    the meaningful boundary.

    Attributes:
        core_domain: dict from sequential core index to domain id
            (the nodeset string from the XML).
        gpu_domain: dict from sequential GPU index to domain id.
    """

    def __init__(self, hwloc_xml):
        self.core_domain = {}
        self.gpu_domain = {}
        self.hostname = None
        # Sequential counters: cores and CoProc OSDevs are
        # numbered 0..N-1 in document order, mirroring how flux R
        # encode and the fluxion amender number resources. We
        # cannot key on hwloc's os_index because on dual-socket
        # AMD systems os_index can collide between packages (the
        # kernel reuses physical CPU IDs per socket), and we
        # cannot key on the OSDev name's trailing digit because
        # for AMD OpenCL devices (opencl0d3 etc.) that digit
        # disagrees with what the amender's get_next_id assigns.
        self._next_core_id = 0
        self._next_gpu_id = 0
        # PCIDev elements that have already contributed a GPU.
        # A physical GPU can expose multiple compute OSDev faces
        # (e.g. cuda0 + nvml0 for NVIDIA when both backends are
        # loaded, or opencl0d0 + rsmi0 for AMD) under one
        # PCIDev. Without deduplication these would count as
        # separate GPUs and inflate the schedulable pool.
        self._seen_pci = set()
        root = ET.fromstring(hwloc_xml)
        self._extract_hostname(root)
        self._index_topology(root, [])

    def _extract_hostname(self, root):
        """Pull HostName from a top-level ``<info>`` element, if
        present. hwloc emits ``<info name="HostName" value="..."/>``
        either as a child of the root or under the Machine object;
        ``root.iter`` finds it in either place. Recording this
        lets results files identify which physical topology a
        benchmark ran against."""
        for info in root.iter("info"):
            if info.get("name") == "HostName":
                self.hostname = info.get("value")
                return

    def describe(self):
        """Return a dict summarizing what was indexed from the XML.

        Useful for diagnosing mismatches between the predicate's
        view of a topology and the IDs that appear in a job's R.
        If ``score_job`` returns 0 for an obviously local job,
        compare ``describe()["core_domain"].keys()`` with the
        cores listed in R: any core ID in R that isn't a key
        here will be silently treated as not-local.

        Returns:
            dict with keys ``hostname`` (from the XML's HostName
            info element, or None), ``core_count``, ``gpu_count``,
            ``domains`` (sorted list of unique domain ids),
            ``core_domain``, and ``gpu_domain`` (the full maps,
            sorted).
        """
        return {
            "hostname": self.hostname,
            "core_count": len(self.core_domain),
            "gpu_count": len(self.gpu_domain),
            "domains": sorted(
                set(self.core_domain.values()) | set(self.gpu_domain.values())
            ),
            "core_domain": dict(sorted(self.core_domain.items())),
            "gpu_domain": dict(sorted(self.gpu_domain.items())),
        }

    def summary(self):
        """Human-readable summary of the indexed topology, with
        a per-domain breakdown.

        Format: a header line with overall counts, then one line
        per domain showing its cores and GPUs as compact idsets.
        Use to confirm the predicate's view matches the expected
        topology before trusting score values from a run.

        Example output for an AMD corona node::

            host=corona11, 48 cores, 4 gpus, 8 domains
              0: core[0-5]
              1: core[6-11], gpu[0]
              2: core[12-17], gpu[1]
              3: core[18-23]
              4: core[24-29]
              5: core[30-35], gpu[2]
              6: core[36-41]
              7: core[42-47], gpu[3]

        Domains are numbered by the first resource (lowest core
        ID, or lowest GPU ID for GPU-only domains) for stable
        ordering. The numeric labels are display-only; the
        underlying domain identifiers (nodeset strings) appear
        in :meth:`describe`'s ``core_domain`` / ``gpu_domain``
        maps.
        """
        header = []
        if self.hostname:
            header.append(f"host={self.hostname}")
        header.append(f"{len(self.core_domain)} cores")
        header.append(f"{len(self.gpu_domain)} gpus")

        # Invert: domain -> sorted list of cores / gpus.
        cores_by_dom = {}
        gpus_by_dom = {}
        for cid, dom in self.core_domain.items():
            cores_by_dom.setdefault(dom, []).append(cid)
        for gid, dom in self.gpu_domain.items():
            gpus_by_dom.setdefault(dom, []).append(gid)
        all_doms = set(cores_by_dom) | set(gpus_by_dom)
        header.append(f"{len(all_doms)} domains")
        head_str = ", ".join(header)

        if not all_doms:
            return head_str

        # Order domains by first-resource ID so output is stable
        # and reads top-to-bottom the way the topology was walked.
        # Core-bearing domains come first, sorted by first core;
        # GPU-only domains sort after, by first GPU. (Mixing
        # core and GPU IDs in one sort key reads counter-
        # intuitively when GPU 0 sits in a domain that has
        # higher-numbered cores than another, all-cores domain.)
        def _first(dom):
            cs = cores_by_dom.get(dom)
            if cs:
                return (0, cs[0])
            return (1, gpus_by_dom[dom][0])

        lines = [head_str]
        for i, dom in enumerate(sorted(all_doms, key=_first)):
            bits = []
            if cores_by_dom.get(dom):
                bits.append(f"core[{IDset(cores_by_dom[dom])}]")
            if gpus_by_dom.get(dom):
                bits.append(f"gpu[{IDset(gpus_by_dom[dom])}]")
            lines.append(f"  {i}: {', '.join(bits)}")
        return "\n".join(lines)

    def _domain_of(self, obj, ancestor_chain):
        """Return the locality-domain id for ``obj``.

        Default: the hwloc ``nodeset`` attribute, falling back to
        the nearest ancestor's nodeset for objects (like GPU
        OSDevs) that don't carry one directly. Returns None if
        nothing in the chain has a nodeset.

        Override for alternative locality rules: socket granularity
        (nearest Package's ``cpuset``), L3 cache (nearest Cache
        with ``depth="3"``), or strict-NUMA-only (return None when
        no nodeset).
        """
        ns = obj.get("nodeset")
        if ns:
            return ns
        for ancestor in reversed(ancestor_chain):
            ns = ancestor.get("nodeset")
            if ns:
                return ns
        return None

    def _index_topology(self, obj, ancestors):
        """Walk the XML tree, recording cores and CoProc OSDevs
        with sequential IDs in document order."""
        ancestors.append(obj)
        try:
            otype = obj.get("type")
            if otype == "Core":
                dom = self._domain_of(obj, ancestors[:-1])
                if dom is not None:
                    self.core_domain[self._next_core_id] = dom
                # Always increment so subsequent core IDs stay
                # aligned with what R produces, even if some core
                # somehow lacks a domain.
                self._next_core_id += 1
            elif otype == "OSDev" and _is_compute_gpu(obj):
                # See :func:`_is_compute_gpu` for the criterion.
                # CUDA / OpenCL appear as osdev_type=5 (CoProc);
                # RSMI / NVML / NVIDIA appear as osdev_type=1 (GPU)
                # alongside Linux DRM display nodes (card*, renderD*,
                # controlD*) — the name filter distinguishes the two.
                #
                # Dedupe: a single physical GPU can expose multiple
                # compute OSDev faces under one PCIDev (e.g. NVIDIA
                # with both CUDA and NVML backends loaded, or AMD
                # with both OpenCL and RSMI). Count only the first
                # face per PCIDev so the schedulable pool reflects
                # physical devices, not backend instances.
                pci_key = None
                for ancestor in reversed(ancestors[:-1]):
                    if ancestor.get("type") == "PCIDev":
                        pci_key = id(ancestor)
                        break
                if pci_key is None or pci_key not in self._seen_pci:
                    if pci_key is not None:
                        self._seen_pci.add(pci_key)
                    dom = self._domain_of(obj, ancestors[:-1])
                    if dom is not None:
                        self.gpu_domain[self._next_gpu_id] = dom
                    self._next_gpu_id += 1
            for child in obj:
                self._index_topology(child, ancestors)
        finally:
            ancestors.pop()

    def score_job(self, r_dict, slot_cores, slot_gpus, nslots):
        """Score one job's placement.

        Returns ``(satisfied, total)`` where ``total == nslots`` and
        ``satisfied`` is the number of slots whose cores and GPUs
        all share a single domain. Capped at ``nslots`` so the
        per-job ratio is bounded by 1.0.
        """
        satisfied = 0
        for entry in r_dict.get("execution", {}).get("R_lite", []):
            n_ranks = len(_parse_idset_string(entry.get("rank", "")))
            ch = entry.get("children", {})
            satisfied += n_ranks * self._count_on_rank(
                _parse_idset_string(ch.get("core", "")),
                _parse_idset_string(ch.get("gpu", "")),
                slot_cores,
                slot_gpus,
            )
        return (min(satisfied, nslots), nslots)

    def score_job_verbose(self, r_dict, slot_cores, slot_gpus, nslots):
        """Like :meth:`score_job` but returns the per-R_lite-entry
        breakdown alongside the score.

        Use when ``score_job`` produces unexpected results. The
        breakdown identifies the two common failure modes:

        - **ID-scheme mismatch**: ``unknown_cores`` / ``unknown_gpus``
          contain IDs that appeared in R but not in
          :attr:`core_domain` / :attr:`gpu_domain`. The predicate
          treats unknown IDs as not-local; if R is using a
          different ID scheme than the XML walk produces, scores
          will silently come out lower than reality.

        - **Domain split**: ``cores_by_domain`` and
          ``gpus_by_domain`` show how the rank's resources
          distribute. A satisfied slot needs both cores and (if
          ``slot_gpus > 0``) GPUs from the same domain in
          quantities matching the slot shape.

        Returns:
            dict with keys ``satisfied``, ``total``, and
            ``breakdown`` (a list of per-R_lite-entry dicts).
            JSON-serializable.
        """
        breakdown = []
        satisfied_total = 0
        for entry in r_dict.get("execution", {}).get("R_lite", []):
            rank_str = entry.get("rank", "")
            ranks = _parse_idset_string(rank_str)
            ch = entry.get("children", {})
            cores = _parse_idset_string(ch.get("core", ""))
            gpus = _parse_idset_string(ch.get("gpu", ""))
            unknown_c = [c for c in cores if c not in self.core_domain]
            unknown_g = [g for g in gpus if g not in self.gpu_domain]
            c_by_dom = Counter(
                self.core_domain[c] for c in cores if c in self.core_domain
            )
            g_by_dom = Counter(self.gpu_domain[g] for g in gpus if g in self.gpu_domain)
            per_rank = self._count_on_rank(
                cores,
                gpus,
                slot_cores,
                slot_gpus,
            )
            satisfied_total += len(ranks) * per_rank
            breakdown.append(
                {
                    "ranks": rank_str,
                    "n_ranks": len(ranks),
                    "cores": list(cores),
                    "gpus": list(gpus),
                    "unknown_cores": unknown_c,
                    "unknown_gpus": unknown_g,
                    "cores_by_domain": dict(c_by_dom),
                    "gpus_by_domain": dict(g_by_dom),
                    "slots_satisfied_per_rank": per_rank,
                }
            )
        return {
            "satisfied": min(satisfied_total, nslots),
            "total": nslots,
            "breakdown": breakdown,
        }

    def _count_on_rank(self, cores, gpus, slot_cores, slot_gpus):
        """Complete slots that fit in a single domain on one rank."""
        c = Counter(self.core_domain[i] for i in cores if i in self.core_domain)
        if not slot_gpus:
            return sum(n // slot_cores for n in c.values())
        g = Counter(self.gpu_domain[i] for i in gpus if i in self.gpu_domain)
        return sum(min(c[d] // slot_cores, g[d] // slot_gpus) for d in c)


def _r_from_event(ev):
    """Return R from a journal alloc event, or None.

    The journal watcher (JournalConsumer) attaches R as the ``R``
    property on alloc events; the per-job eventlog watcher does
    not, and the caller must fall back to a KVS lookup.
    """
    return getattr(ev, "R", None)


class LocalityBenchmark(Benchmark):
    """Submit multi-slot jobs and score per-slot locality.

    Submits ``njobs`` jobs of ``nslots`` slots each, with each slot
    requesting ``slot_cores`` cores and ``slot_gpus`` GPUs. The
    duration mode controls job lifetime:

    * fixed FSD (default ``0.1s``): every job runs that long.
    * ``fill``: jobs never complete; saturate then cancel.
    * ``random[:LO-HI]``: uniform random duration per job
      (default range ``0.1s-1.0s``).

    Each job's placement is scored by :class:`LocalityPredicate`
    as its alloc event arrives — the journal watcher carries R
    in the event context (no extra lookup); the per-job eventlog
    watcher takes one KVS lookup per alloc. Headline metric is
    the mean per-job locality fraction. Requires
    ``--hwloc-xml-path``.
    """

    name = "locality"
    stages = ("submit", "execute")
    description = (
        "Score how well the scheduler packs each slot's cores and "
        "GPUs into a single locality domain (NUMA/socket)."
    )

    #: Locality score is the headline. ``num_jobs_scored`` is a
    #: confidence indicator — divergence from ``njobs`` means some
    #: alloc events came in without R available, reducing the
    #: sample size behind the mean.
    SUMMARY_METRICS = (
        ("njobs", "jobs", "count"),
        ("mean_locality_score", "locality score", "fraction"),
        ("num_jobs_scored", "jobs scored", "count"),
        ("throughput", "throughput", "rate"),
        ("submit_rate", "submit rate", "rate"),
        ("alloc_rate", "alloc rate", "rate"),
    )

    RESULT = ("mean_locality_score", "")

    REPORT_HEADINGS = {
        **COMMON_REPORT_HEADINGS,
        "nslots": "NSLOTS",
        "duration": "DUR",
        "mean_locality_score": "LOC",
        "throughput": "THRPUT",
    }

    REPORT_FORMATS = {
        "default": {
            "description": "Locality summary: identity columns plus "
            "the headline locality score and throughput",
            "format": "{scheduler:<14.14+}  {real_exec:<4}  "
            "?:{tag:<8.8+}  "
            "{nodes:>5}  {njobs:>6}  {nslots:>6}  "
            "?:{duration:<14.14+}  "
            "{mean_locality_score:>5.3f}  {throughput:>7.0f}",
        },
        "long": {
            "description": "All locality config and metric columns",
            "format": "{time:<16.16}  {scheduler:<14.14+}  "
            "{real_exec:<4}  "
            "?:{tag:<8.8+}  ?:{watcher:<8.8+}  "
            "{nodes:>5}  {cores:>5}  {gpus:>4}  {njobs:>6}  "
            "{nslots:>6}  ?:{duration:<14.14+}  "
            "{submit_rate:>7.0f}  {alloc_rate:>7.0f}  "
            "{mean_locality_score:>5.3f}  {throughput:>7.0f}",
        },
    }

    def __init__(
        self,
        njobs,
        slot_cores=1,
        slot_gpus=0,
        nslots=1,
        duration_mode="fixed",
        duration_lo="0.1s",
        duration_hi="0.1s",
        hwloc_xml=None,
        watcher_factory=None,
        real_exec=False,
    ):
        if hwloc_xml is None:
            raise ValueError(
                "LocalityBenchmark requires hwloc_xml; pass "
                "--hwloc-xml-path on the CLI"
            )
        self.njobs = njobs
        self.slot_cores = slot_cores
        self.slot_gpus = slot_gpus
        self.nslots = nslots
        self.duration_mode = duration_mode
        self.duration_lo = duration_lo
        self.duration_hi = duration_hi
        self.predicate = LocalityPredicate(hwloc_xml)
        self.watcher_factory = watcher_factory
        self.real_exec = real_exec

    @classmethod
    def register_options(cls, group):
        group.add_argument(
            "--duration",
            default="0.1s",
            metavar="FSD|fill|random[:LO-HI]",
            help="job duration mode: an FSD string for fixed "
            "duration (e.g. '0.1s'), 'fill' to saturate the "
            "cluster with never-completing jobs and cancel after "
            "placement is observed, or 'random[:LO-HI]' for "
            "uniform random durations (default range: "
            "0.1s-1.0s). Default: 0.1s.",
        )
        group.add_argument(
            "--nslots",
            type=int,
            default=1,
            metavar="N",
            help="slots per job (default: 1). Each slot requests "
            "--slot-cores cores and --slot-gpus GPUs.",
        )

    @classmethod
    def from_args(cls, args, resources, watcher_factory):
        if not getattr(args, "hwloc_xml_path", None):
            raise ValueError(
                "locality benchmark requires --hwloc-xml-path "
                "(hwloc XML describing the per-node topology)"
            )
        with open(args.hwloc_xml_path) as f:
            hwloc_xml = f.read()
        mode, lo, hi = _parse_duration_mode(args.duration)
        # fill mode ignores --njobs and saturates instead. Each
        # job's footprint is nslots * slot_{cores,gpus} per rank.
        if mode == "fill":
            njobs = saturation_count(
                resources["nodes"],
                resources["cores_per_node"],
                resources["gpus_per_node"],
                slot_cores=args.nslots * args.slot_cores,
                slot_gpus=args.nslots * args.slot_gpus,
            )
        else:
            njobs = args.njobs
        bench = cls(
            njobs=njobs,
            slot_cores=args.slot_cores,
            slot_gpus=args.slot_gpus,
            nslots=args.nslots,
            duration_mode=mode,
            duration_lo=lo or "0.1s",
            duration_hi=hi or "0.1s",
            hwloc_xml=hwloc_xml,
            watcher_factory=watcher_factory,
            real_exec=args.real_exec,
        )
        # Log the predicate summary HERE, not in run(): from_args
        # is invoked during _build_benchmark before the framework
        # calls emitter.test_start(), so log output here sits
        # outside the UI's live-redraw region (alongside the
        # existing "launching: ..." log line) and stays visible.
        # A LOGGER.info from inside run() lands inside the live
        # region and gets overwritten by the next UI redraw.
        if getattr(args, "verbose", False):
            LOGGER.info(
                "locality predicate: %s",
                bench.predicate.summary(),
            )
        return bench

    @classmethod
    def config_dict(cls, args):
        return {
            "njobs": args.njobs,
            "slot_cores": args.slot_cores,
            "slot_gpus": args.slot_gpus,
            "nslots": args.nslots,
            "duration": args.duration,
        }

    def _build_jobspec(self, duration_fsd):
        """Build a jobspec.

        Pass an FSD string for a normal-duration job, or ``None``
        for a job that never completes naturally (used for
        ``--duration=fill``).
        """
        kwargs = {
            "ntasks": self.nslots,
            "cores_per_task": self.slot_cores,
        }
        if self.slot_gpus:
            kwargs["gpus_per_task"] = self.slot_gpus
        if self.real_exec:
            sleep_arg = (
                "inf"
                if duration_fsd is None
                else str(flux.util.parse_fsd(duration_fsd))
            )
            return JobspecV1.from_submit(["sleep", sleep_arg], **kwargs)
        kwargs["attributes"] = {
            "system.exec.test.run_duration": (
                "0" if duration_fsd is None else duration_fsd
            ),
        }
        return JobspecV1.from_submit(["true"], **kwargs)

    def run(self, handle, emitter):
        """Submit jobs, observe placement, score, return metrics."""
        # 1-element lists carry phase timestamps across closures.
        t_all_submitted = [None]
        t_all_alloc = [None]
        t_all_done = [None]
        # Per-job locality scores and breakdowns accumulate here
        # as alloc events arrive. The journal watcher carries R
        # in the event context (synchronous score); the per-job
        # eventlog watcher does not, so we kick off an async KVS
        # lookup whose .then() callback does the scoring without
        # blocking the reactor.
        scores = []
        breakdowns = []
        # In-flight async KVS lookups. Fill mode defers bulk.stop()
        # until this hits zero so no allocations go unscored.
        pending_lookups = [0]
        deferred_stop_bulk = [None]

        submit_tracker = Tracker(self.njobs, "job")
        done_tracker = Tracker(
            self.njobs,
            "job",
            gate=lambda: t_all_submitted[0] is not None,
        )
        # In fill mode, alloc is the terminal event (jobs are then
        # cancelled); otherwise clean is terminal.
        done_event = "alloc" if self.duration_mode == "fill" else "clean"

        def score_r(r, jobid=None):
            # Always use the verbose path so the per-job
            # breakdown can be included in the results file —
            # the overhead per job is negligible, and inspecting
            # a recorded run's R-mapping is the primary tool for
            # diagnosing surprising scores.
            breakdown = self.predicate.score_job_verbose(
                r,
                self.slot_cores,
                self.slot_gpus,
                self.nslots,
            )
            satisfied = breakdown["satisfied"]
            total = breakdown["total"]
            scores.append(satisfied / total if total else 0)
            breakdowns.append(
                {
                    "jobid": (flux.job.JobID(jobid).f58 if jobid is not None else None),
                    **breakdown,
                }
            )

        def make_lookup_complete(jobid):
            """Build a .then() callback that knows its jobid so
            recorded breakdowns can be labelled with the job they
            describe. (Bare closure over loop variable wouldn't
            work — every late callback would see the last jobid.)"""

            def on_lookup_complete(future):
                pending_lookups[0] -= 1
                try:
                    data = future.get_decode()
                except OSError:
                    data = []
                for entry in data:
                    r = entry.get("R")
                    if r is not None:
                        score_r(r, jobid)
                        break
                # If fill-mode stop was deferred waiting on us, fire
                # it now that the last lookup has drained.
                if deferred_stop_bulk[0] is not None and pending_lookups[0] == 0:
                    bulk = deferred_stop_bulk[0]
                    deferred_stop_bulk[0] = None
                    bulk.stop()

            return on_lookup_complete

        def on_event(_bulk, jobid, ev):
            if ev.name == "submit":
                submit_tracker.step(emitter)
            elif ev.name == "alloc":
                r = _r_from_event(ev)
                if r is not None:
                    score_r(r, jobid)
                else:
                    # Per-job watcher: R isn't in the event, fetch
                    # asynchronously and let .then() score it.
                    pending_lookups[0] += 1
                    lookup = flux.job.JobKVSLookup(
                        handle,
                        ids=[jobid],
                        keys=["R"],
                    )
                    lookup.fetch_data().then(
                        make_lookup_complete(jobid),
                    )
                if done_event == "alloc":
                    done_tracker.step(emitter)
            elif ev.name == done_event:
                done_tracker.step(emitter)

        def on_all_submitted(_bulk):
            t_all_submitted[0] = time.time()
            emitter.stage("execute", 1, 2)
            done_tracker.flush(emitter)

        def on_all_alloc(bulk):
            t_all_alloc[0] = time.time()
            if self.duration_mode == "fill":
                # All allocated; cancel them. R lookups already
                # in flight need to drain before we stop the
                # reactor so no allocations go unscored.
                t_all_done[0] = time.time()
                bulk.cancelall()
                if pending_lookups[0] == 0:
                    bulk.stop()
                else:
                    deferred_stop_bulk[0] = bulk

        def on_all_clean(_bulk):
            if self.duration_mode != "fill":
                t_all_done[0] = time.time()

        bulk_kwargs = {
            "events_of_interest": (
                "submit",
                "alloc",
                "exception",
                "clean",
            ),
        }
        if self.watcher_factory is not None:
            bulk_kwargs["watcher_factory"] = self.watcher_factory
        bulk = BulkRun(handle, **bulk_kwargs)

        # Submit.  fixed/fill share one jobspec across all jobs;
        # random builds one jobspec per job since each has its own
        # sampled duration.
        if self.duration_mode == "fixed":
            bulk.push_jobs(self._build_jobspec(self.duration_lo), self.njobs)
        elif self.duration_mode == "fill":
            bulk.push_jobs(self._build_jobspec(None), self.njobs)
        elif self.duration_mode == "random":
            lo_sec = flux.util.parse_fsd(self.duration_lo)
            hi_sec = flux.util.parse_fsd(self.duration_hi)
            for _ in range(self.njobs):
                d = random.uniform(lo_sec, hi_sec)
                bulk.push_jobs(
                    self._build_jobspec(flux.util.fsd(d)),
                    1,
                )
        else:
            raise ValueError(f"unknown duration mode: {self.duration_mode!r}")

        bulk.add_event_cb(on_event)
        bulk.add_bulk_event_cb("submit", on_all_submitted)
        bulk.add_bulk_event_cb("alloc", on_all_alloc)
        bulk.add_bulk_event_cb("clean", on_all_clean)

        emitter.stage("submit", 0, 2)
        t_submit_start = time.time()
        result = bulk.run()
        t_done_wall = time.time()

        if result.njobs == 0:
            raise RuntimeError(
                failure_diagnostic(
                    result,
                    "locality: all submits failed",
                )
            )
        if t_all_alloc[0] is None:
            raise RuntimeError(
                failure_diagnostic(
                    result,
                    "locality: no job reached 'alloc' event",
                )
            )
        if not scores:
            raise RuntimeError(
                failure_diagnostic(
                    result,
                    "locality: no R values could be scored",
                )
            )

        # Rate-denominator convention matches the other benchmarks;
        # see benchmarks.py module docstring for the rationale.
        submit_rate = result.njobs / (t_all_submitted[0] - t_submit_start)
        alloc_rate = result.njobs / (t_all_alloc[0] - t_submit_start)
        end_time = t_all_done[0] or t_done_wall
        throughput = result.njobs / (end_time - t_submit_start)

        if self.duration_mode == "fixed":
            duration_str = self.duration_lo
        elif self.duration_mode == "fill":
            duration_str = "fill"
        else:
            duration_str = f"random:{self.duration_lo}-{self.duration_hi}"

        return {
            "njobs": result.njobs,
            "nslots": self.nslots,
            "duration": duration_str,
            "duration_mode": self.duration_mode,
            "t_submit_start": t_submit_start,
            "t_all_submitted": t_all_submitted[0],
            "t_all_alloc": t_all_alloc[0],
            "t_all_done": t_all_done[0],
            "t_done": t_done_wall,
            "submit_rate": submit_rate,
            "alloc_rate": alloc_rate,
            "throughput": throughput,
            "mean_locality_score": sum(scores) / len(scores),
            "num_jobs_scored": len(scores),
            "locality_score_distribution": scores,
            # Recorded for post-hoc analysis: the predicate's
            # view of the topology (lets a reader confirm the
            # right XML was used, and exposes the core_domain /
            # gpu_domain maps for inspection), and a per-job
            # breakdown (cores/gpus by domain, unknown IDs in R,
            # slots satisfied per rank) — the primary tool for
            # explaining why a score came out the way it did.
            "locality_topology": self.predicate.describe(),
            "locality_score_breakdown": breakdowns,
        }


# vi: ts=4 sw=4 expandtab
