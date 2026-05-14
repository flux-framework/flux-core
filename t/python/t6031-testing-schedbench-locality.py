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

"""Unit tests for flux.testing.schedbench.locality.

The benchmark's :class:`LocalityPredicate` is pure logic (hwloc XML
+ R dict -> score), so it earns straightforward unit tests without
needing a broker. Test topologies are built programmatically via
ElementTree so each test reads as a self-contained scenario rather
than a reference into a separately-shipped fixture file.
"""

import unittest
import xml.etree.ElementTree as ET

import subflux  # noqa: F401
from flux.testing.schedbench import LocalityPredicate
from flux.testing.schedbench.locality import (
    _parse_duration_mode,
    _parse_idset_string,
    _r_from_event,
)

# ---------------------------------------------------------------
# Topology fixture builder
# ---------------------------------------------------------------


def _build_topo(domains):
    """Build a minimal hwloc XML topology string.

    Each entry in ``domains`` is a dict::

        {"nodeset": "0x1",
         "cores":  [0, 1, 2, 3],      # core os_index values (optional)
         "gpus":   ["cuda0", ...]}    # GPU OSDev names (optional)

    Cores carry the domain's nodeset directly; GPUs are wrapped in a
    PCIDev that carries the nodeset, exercising the predicate's
    "OSDev inherits nodeset from its parent" path.

    Returns the XML as a string suitable for
    :class:`LocalityPredicate`.
    """
    root = ET.Element("topology", version="2.0")
    machine = ET.SubElement(root, "object", type="Machine")
    for dom in domains:
        ns = dom["nodeset"]
        # A Group with a NUMANode sibling is how modern hwloc
        # actually structures NUMA, even though our predicate
        # doesn't depend on the Group/NUMANode pair specifically —
        # it reads ``nodeset`` off whichever ancestor has one.
        grp = ET.SubElement(machine, "object", type="Group", nodeset=ns)
        ET.SubElement(grp, "object", type="NUMANode", nodeset=ns)
        for core_idx in dom.get("cores", []):
            ET.SubElement(
                grp,
                "object",
                type="Core",
                os_index=str(core_idx),
                nodeset=ns,
            )
        for gpu_name in dom.get("gpus", []):
            # PCIDev carries the nodeset; OSDev (the GPU) does not.
            # This exercises the ancestor-chain fallback in
            # _domain_of. osdev_type=5 is hwloc's CoProc
            # encoding for compute GPUs.
            pci = ET.SubElement(grp, "object", type="PCIDev", nodeset=ns)
            ET.SubElement(
                pci,
                "object",
                type="OSDev",
                name=gpu_name,
                osdev_type="5",
            )
    return ET.tostring(root, encoding="unicode")


def _r(rank="0", core="", gpu=""):
    """Build a minimal RFC 20 R dict with one R_lite entry."""
    children = {"core": core}
    if gpu:
        children["gpu"] = gpu
    return {
        "execution": {
            "R_lite": [{"rank": rank, "children": children}],
        },
    }


# ---------------------------------------------------------------
# LocalityPredicate
# ---------------------------------------------------------------


class TestLocalityPredicateIndexing(unittest.TestCase):
    """How the predicate maps cores and GPUs to domain identifiers."""

    def test_cores_indexed_by_nodeset(self):
        """Each Core's domain id is the hwloc nodeset attribute."""
        xml = _build_topo(
            [
                {"nodeset": "0x1", "cores": [0, 1]},
                {"nodeset": "0x2", "cores": [2, 3]},
            ]
        )
        pred = LocalityPredicate(xml)
        self.assertEqual(pred.core_domain[0], "0x1")
        self.assertEqual(pred.core_domain[1], "0x1")
        self.assertEqual(pred.core_domain[2], "0x2")
        self.assertEqual(pred.core_domain[3], "0x2")

    def test_two_cores_same_nodeset_share_domain(self):
        """Cores with the same nodeset share an id; the
        identifier is opaque but stable per nodeset."""
        xml = _build_topo([{"nodeset": "0x1", "cores": [0, 1, 2, 3]}])
        pred = LocalityPredicate(xml)
        self.assertEqual(
            len({pred.core_domain[i] for i in (0, 1, 2, 3)}),
            1,
        )

    def test_gpu_inherits_nodeset_from_pcidev(self):
        """OSDev (GPU) doesn't carry its own nodeset; it
        inherits the enclosing PCIDev's via the ancestor walk
        in :meth:`_domain_of`."""
        xml = _build_topo(
            [
                {"nodeset": "0x1", "cores": [0], "gpus": ["cuda0"]},
                {"nodeset": "0x2", "cores": [1], "gpus": ["cuda1"]},
            ]
        )
        pred = LocalityPredicate(xml)
        self.assertEqual(pred.gpu_domain[0], "0x1")
        self.assertEqual(pred.gpu_domain[1], "0x2")

    def test_gpus_indexed_sequentially_in_document_order(self):
        """GPUs are keyed by their position in document order
        (0, 1, 2, ...), not by the digit in their OSDev name.

        For AMD OpenCL devices named ``opencl0d{N}``, the digit
        happens to match document order only by convention. The
        amender uses sequential numbering (``int("0d3")`` raises
        and falls back to ``get_next_id``), so the predicate
        does the same to keep IDs aligned with R.
        """
        xml = _build_topo(
            [
                {"nodeset": "0x1", "gpus": ["nvidia7"]},
                {"nodeset": "0x2", "gpus": ["nvidia3"]},
            ]
        )
        pred = LocalityPredicate(xml)
        # First GPU walked (nvidia7) -> id 0; second (nvidia3) -> 1.
        self.assertEqual(pred.gpu_domain[0], "0x1")
        self.assertEqual(pred.gpu_domain[1], "0x2")

    def test_cores_indexed_sequentially_in_document_order(self):
        """Cores are keyed by their position in document order,
        not by the hwloc ``os_index`` attribute, even if os_index
        is present and non-contiguous.

        Real-world reason: on multi-socket AMD systems hwloc
        reports per-socket physical CPU IDs that collide between
        packages, so an os_index-keyed map silently overwrites
        half the cores when both sockets are present.
        """
        # Hand-written XML with sparse, non-contiguous os_index
        # values — mimics what hwloc emits on AMD Zen.
        xml = """\
<topology version="2.0">
  <object type="Machine">
    <object type="Group" nodeset="0x1">
      <object type="NUMANode" nodeset="0x1"/>
      <object type="Core" os_index="0" nodeset="0x1"/>
      <object type="Core" os_index="1" nodeset="0x1"/>
      <object type="Core" os_index="2" nodeset="0x1"/>
      <object type="Core" os_index="4" nodeset="0x1"/>
      <object type="Core" os_index="5" nodeset="0x1"/>
      <object type="Core" os_index="6" nodeset="0x1"/>
    </object>
  </object>
</topology>"""
        pred = LocalityPredicate(xml)
        # All 6 cores in domain 0x1, keyed by sequential 0..5
        # (NOT by os_index, which would have a gap at 3 and max 6).
        self.assertEqual(sorted(pred.core_domain.keys()), [0, 1, 2, 3, 4, 5])
        self.assertTrue(all(d == "0x1" for d in pred.core_domain.values()))

    def test_os_index_collision_does_not_lose_cores(self):
        """Two cores with the same os_index in different
        packages must both be indexed. This is the corona11.xml
        regression: dual-socket AMD systems reuse physical CPU
        IDs per socket, and the previous os_index-keyed scheme
        silently dropped half the cores."""
        xml = """\
<topology version="2.0">
  <object type="Machine">
    <object type="Package">
      <object type="Group" nodeset="0x1">
        <object type="NUMANode" nodeset="0x1"/>
        <object type="Core" os_index="0" nodeset="0x1"/>
        <object type="Core" os_index="1" nodeset="0x1"/>
      </object>
    </object>
    <object type="Package">
      <object type="Group" nodeset="0x2">
        <object type="NUMANode" nodeset="0x2"/>
        <object type="Core" os_index="0" nodeset="0x2"/>
        <object type="Core" os_index="1" nodeset="0x2"/>
      </object>
    </object>
  </object>
</topology>"""
        pred = LocalityPredicate(xml)
        # All 4 cores indexed under unique sequential ids,
        # mapping to their respective NUMA domains in walk order.
        self.assertEqual(len(pred.core_domain), 4)
        self.assertEqual(pred.core_domain[0], "0x1")
        self.assertEqual(pred.core_domain[1], "0x1")
        self.assertEqual(pred.core_domain[2], "0x2")
        self.assertEqual(pred.core_domain[3], "0x2")

    def test_display_gpus_not_counted(self):
        """Linux DRM nodes (card*, renderD*, controlD*) appear in
        hwloc XML as OSDev with osdev_type=1 and sit alongside
        the CoProc OSDev under the same PCIDev. They're three
        faces of one physical device, not three GPUs, and aren't
        schedulable compute — the predicate must skip them.

        Without this check, a system with one AMD GPU would
        score as four GPUs (card / renderD / controlD / opencl)
        and overcount the schedulable pool by 4x.
        """
        # Build the XML by hand so we can mix osdev_type=1
        # (display) and osdev_type=5 (CoProc) under one PCIDev.
        xml = """\
<topology version="2.0">
  <object type="Machine">
    <object type="Group" nodeset="0x1">
      <object type="NUMANode" nodeset="0x1"/>
      <object type="PCIDev" nodeset="0x1">
        <object type="OSDev" name="card0" osdev_type="1"/>
        <object type="OSDev" name="renderD128" osdev_type="1"/>
        <object type="OSDev" name="controlD64" osdev_type="1"/>
        <object type="OSDev" name="opencl0d0" osdev_type="5"/>
      </object>
    </object>
  </object>
</topology>"""
        pred = LocalityPredicate(xml)
        # Only the CoProc node makes it in, with id 0.
        self.assertEqual(list(pred.gpu_domain.keys()), [0])

    def test_display_only_card_yields_no_gpus(self):
        """A PCIDev with display OSDev nodes but no CoProc
        (e.g. an integrated VGA controller, or an older AMD
        driver without OpenCL loaded) contributes zero GPUs to
        the schedulable pool — the corresponding NUMA domain
        appears in the topology but has no gpu_domain entry."""
        xml = """\
<topology version="2.0">
  <object type="Machine">
    <object type="Group" nodeset="0x1">
      <object type="NUMANode" nodeset="0x1"/>
      <object type="Core" os_index="0" nodeset="0x1"/>
      <object type="PCIDev" nodeset="0x1">
        <object type="OSDev" name="card0" osdev_type="1"/>
        <object type="OSDev" name="controlD64" osdev_type="1"/>
      </object>
    </object>
  </object>
</topology>"""
        pred = LocalityPredicate(xml)
        # Core still indexed (id 0); no GPUs registered.
        self.assertIn(0, pred.core_domain)
        self.assertEqual(pred.gpu_domain, {})

    def test_rsmi_gpus_counted_despite_osdev_type_1(self):
        """AMD ROCm devices arrive from the RSMI backend with
        ``osdev_type=1`` (GPU) — the same value Linux DRM
        display nodes use. A pure osdev_type=5 filter would
        miss them; the predicate must use a combined name +
        type criterion.

        Regression test for an actual corona system that
        reported 0 GPUs because only osdev_type=5 was matched.
        """
        xml = """\
<topology version="2.0">
  <object type="Machine">
    <object type="Group" nodeset="0x1">
      <object type="NUMANode" nodeset="0x1"/>
      <object type="PCIDev" nodeset="0x1">
        <object type="OSDev" name="rsmi0" osdev_type="1"/>
      </object>
    </object>
    <object type="Group" nodeset="0x2">
      <object type="NUMANode" nodeset="0x2"/>
      <object type="PCIDev" nodeset="0x2">
        <object type="OSDev" name="rsmi1" osdev_type="1"/>
      </object>
    </object>
  </object>
</topology>"""
        pred = LocalityPredicate(xml)
        self.assertEqual(len(pred.gpu_domain), 2)
        self.assertEqual(pred.gpu_domain[0], "0x1")
        self.assertEqual(pred.gpu_domain[1], "0x2")

    def test_nvml_gpus_counted_despite_osdev_type_1(self):
        """NVIDIA NVML backend also reports compute GPUs as
        osdev_type=1 with name nvml{N}. Same path as RSMI."""
        xml = """\
<topology version="2.0">
  <object type="Machine">
    <object type="Group" nodeset="0x1">
      <object type="PCIDev" nodeset="0x1">
        <object type="OSDev" name="nvml0" osdev_type="1"/>
      </object>
    </object>
  </object>
</topology>"""
        pred = LocalityPredicate(xml)
        self.assertEqual(list(pred.gpu_domain.keys()), [0])

    def test_drm_display_with_no_compute_backend_yields_no_gpus(self):
        """A PCIDev with only osdev_type=1 nodes whose names
        match neither the compute prefix list nor anything
        else — purely display."""
        xml = """\
<topology version="2.0">
  <object type="Machine">
    <object type="Group" nodeset="0x1">
      <object type="PCIDev" nodeset="0x1">
        <object type="OSDev" name="card0" osdev_type="1"/>
        <object type="OSDev" name="renderD128" osdev_type="1"/>
        <object type="OSDev" name="controlD64" osdev_type="1"/>
      </object>
    </object>
  </object>
</topology>"""
        pred = LocalityPredicate(xml)
        self.assertEqual(pred.gpu_domain, {})

    def test_dedupe_multiple_compute_faces_per_pcidev(self):
        """One physical GPU can expose multiple compute OSDev
        faces under one PCIDev: NVIDIA with both CUDA and NVML
        loaded, AMD with both OpenCL and RSMI. Each face passes
        :func:`_is_compute_gpu` independently. Without dedupe
        the predicate would count one card as two GPUs."""
        xml = """\
<topology version="2.0">
  <object type="Machine">
    <object type="Group" nodeset="0x1">
      <object type="NUMANode" nodeset="0x1"/>
      <object type="PCIDev" nodeset="0x1">
        <object type="OSDev" name="cuda0" osdev_type="5"/>
        <object type="OSDev" name="nvml0" osdev_type="1"/>
      </object>
    </object>
    <object type="Group" nodeset="0x2">
      <object type="NUMANode" nodeset="0x2"/>
      <object type="PCIDev" nodeset="0x2">
        <object type="OSDev" name="opencl0d0" osdev_type="5"/>
        <object type="OSDev" name="rsmi0" osdev_type="1"/>
      </object>
    </object>
  </object>
</topology>"""
        pred = LocalityPredicate(xml)
        # 2 PCIDevs, each with two compute faces → 2 GPUs total.
        self.assertEqual(len(pred.gpu_domain), 2)
        self.assertEqual(pred.gpu_domain[0], "0x1")
        self.assertEqual(pred.gpu_domain[1], "0x2")

    def test_dedupe_does_not_skip_separate_pcidevs(self):
        """Two PCIDevs each containing one CoProc OSDev — the
        dedup must scope to each PCIDev independently, not
        globally."""
        xml = """\
<topology version="2.0">
  <object type="Machine">
    <object type="Group" nodeset="0x1">
      <object type="PCIDev" nodeset="0x1">
        <object type="OSDev" name="cuda0" osdev_type="5"/>
      </object>
    </object>
    <object type="Group" nodeset="0x2">
      <object type="PCIDev" nodeset="0x2">
        <object type="OSDev" name="cuda1" osdev_type="5"/>
      </object>
    </object>
  </object>
</topology>"""
        pred = LocalityPredicate(xml)
        self.assertEqual(len(pred.gpu_domain), 2)

    def test_dedupe_preserves_sequential_ids(self):
        """When a duplicate face is skipped, the sequential GPU
        counter must NOT advance — otherwise downstream IDs in
        R would mismatch the predicate's domain map."""
        xml = """\
<topology version="2.0">
  <object type="Machine">
    <object type="Group" nodeset="0x1">
      <object type="PCIDev" nodeset="0x1">
        <object type="OSDev" name="cuda0" osdev_type="5"/>
        <object type="OSDev" name="nvml0" osdev_type="1"/>
      </object>
    </object>
    <object type="Group" nodeset="0x2">
      <object type="PCIDev" nodeset="0x2">
        <object type="OSDev" name="cuda1" osdev_type="5"/>
        <object type="OSDev" name="nvml1" osdev_type="1"/>
      </object>
    </object>
  </object>
</topology>"""
        pred = LocalityPredicate(xml)
        # GPU IDs must be 0 and 1 (sequential, no gap from
        # the deduped nvml0/nvml1 faces).
        self.assertEqual(sorted(pred.gpu_domain.keys()), [0, 1])


class TestLocalityPredicateDescribe(unittest.TestCase):
    """The :meth:`describe` diagnostic helper."""

    def test_describe_returns_shape(self):
        xml = _build_topo(
            [
                {"nodeset": "0x1", "cores": [0, 1], "gpus": ["cuda0"]},
                {"nodeset": "0x2", "cores": [2, 3], "gpus": ["cuda1"]},
            ]
        )
        pred = LocalityPredicate(xml)
        info = pred.describe()
        self.assertIsNone(info["hostname"])  # _build_topo emits no HostName
        self.assertEqual(info["core_count"], 4)
        self.assertEqual(info["gpu_count"], 2)
        self.assertEqual(sorted(info["domains"]), ["0x1", "0x2"])
        self.assertEqual(set(info["core_domain"].keys()), {0, 1, 2, 3})
        self.assertEqual(set(info["gpu_domain"].keys()), {0, 1})

    def test_describe_is_serializable(self):
        """describe() output should be JSON-friendly so it can
        be embedded in the results file."""
        import json

        xml = _build_topo([{"nodeset": "0x1", "cores": [0, 1]}])
        pred = LocalityPredicate(xml)
        # Should not raise.
        json.dumps(pred.describe())

    def test_hostname_extracted_from_xml(self):
        """``<info name="HostName" value="..."/>`` anywhere in
        the XML is picked up so results files can identify which
        topology was scored. Real hwloc XMLs put this at the root
        or under the Machine object — both work."""
        xml = """\
<topology version="2.0">
  <info name="HostName" value="corona11"/>
  <object type="Machine">
    <object type="Group" nodeset="0x1">
      <object type="Core" os_index="0" nodeset="0x1"/>
    </object>
  </object>
</topology>"""
        pred = LocalityPredicate(xml)
        self.assertEqual(pred.hostname, "corona11")
        self.assertEqual(pred.describe()["hostname"], "corona11")

    def test_hostname_absent_when_xml_omits_it(self):
        xml = _build_topo([{"nodeset": "0x1", "cores": [0]}])
        pred = LocalityPredicate(xml)
        self.assertIsNone(pred.hostname)


class TestLocalityPredicateSummary(unittest.TestCase):
    """The :meth:`summary` human-readable description, used in
    verbose startup output. Returns a header with counts plus a
    per-domain breakdown line showing which cores and GPUs each
    domain contains."""

    def test_summary_header_includes_counts(self):
        xml = _build_topo(
            [
                {"nodeset": "0x1", "cores": [0, 1], "gpus": ["cuda0"]},
                {"nodeset": "0x2", "cores": [2, 3], "gpus": ["cuda1"]},
            ]
        )
        s = LocalityPredicate(xml).summary()
        head = s.splitlines()[0]
        self.assertIn("4 cores", head)
        self.assertIn("2 gpus", head)
        self.assertIn("2 domains", head)

    def test_summary_includes_hostname_when_present(self):
        xml = """\
<topology version="2.0">
  <info name="HostName" value="corona11"/>
  <object type="Machine">
    <object type="Group" nodeset="0x1">
      <object type="Core" os_index="0" nodeset="0x1"/>
    </object>
  </object>
</topology>"""
        pred = LocalityPredicate(xml)
        self.assertIn("host=corona11", pred.summary())

    def test_summary_omits_hostname_when_absent(self):
        xml = _build_topo([{"nodeset": "0x1", "cores": [0]}])
        s = LocalityPredicate(xml).summary()
        self.assertNotIn("host=", s)

    def test_summary_breakdown_per_domain(self):
        """One indented line per domain showing its cores and
        GPUs as compact idsets. Domains with both cores and a
        GPU show both; domains with only cores show only cores."""
        xml = _build_topo(
            [
                {"nodeset": "0x1", "cores": [0, 1, 2, 3, 4, 5]},
                {"nodeset": "0x2", "cores": [6, 7, 8, 9, 10, 11], "gpus": ["cuda0"]},
                {"nodeset": "0x4", "cores": [12, 13, 14, 15, 16, 17]},
            ]
        )
        s = LocalityPredicate(xml).summary()
        lines = s.splitlines()
        # Header + 3 domain lines.
        self.assertEqual(len(lines), 4)
        self.assertIn("core[0-5]", lines[1])
        self.assertNotIn("gpu", lines[1])
        self.assertIn("core[6-11]", lines[2])
        self.assertIn("gpu[0]", lines[2])
        self.assertIn("core[12-17]", lines[3])
        self.assertNotIn("gpu", lines[3])

    def test_summary_compact_idset_with_gaps(self):
        """Non-contiguous cores within a domain render as
        comma-separated ranges (e.g. ``0-2,4-5``)."""
        # Hand-write XML so we can put non-contiguous cores in
        # one domain without _build_topo's monotonic id sequence
        # masking the case.
        xml = """\
<topology version="2.0">
  <object type="Machine">
    <object type="Group" nodeset="0x1">
      <object type="Core" os_index="0" nodeset="0x1"/>
      <object type="Core" os_index="1" nodeset="0x1"/>
      <object type="Core" os_index="2" nodeset="0x1"/>
    </object>
    <object type="Group" nodeset="0x2">
      <object type="Core" os_index="3" nodeset="0x2"/>
    </object>
    <object type="Group" nodeset="0x1">
      <object type="Core" os_index="4" nodeset="0x1"/>
      <object type="Core" os_index="5" nodeset="0x1"/>
    </object>
  </object>
</topology>"""
        # Sequential ids in document order: 0,1,2 then 3 then 4,5.
        # Cores 0-2 and 4-5 share domain 0x1 -> idset "0-2,4-5".
        s = LocalityPredicate(xml).summary()
        self.assertIn("core[0-2,4-5]", s)

    def test_summary_no_domains_returns_header_only(self):
        """An empty topology gives just the header line."""
        xml = '<topology><object type="Machine"></object></topology>'
        s = LocalityPredicate(xml).summary()
        self.assertEqual(len(s.splitlines()), 1)
        self.assertIn("0 cores", s)
        self.assertIn("0 domains", s)


class TestLocalityPredicateScoreJobVerbose(unittest.TestCase):
    """The :meth:`score_job_verbose` diagnostic helper. The
    breakdown it returns is the primary tool for debugging score
    anomalies in production runs."""

    def _topo_2numa_with_gpus(self):
        return _build_topo(
            [
                {"nodeset": "0x1", "cores": [0, 1, 2, 3], "gpus": ["cuda0"]},
                {"nodeset": "0x2", "cores": [4, 5, 6, 7], "gpus": ["cuda1"]},
            ]
        )

    def test_verbose_reports_satisfied_total(self):
        """The top-level (satisfied, total) matches score_job."""
        pred = LocalityPredicate(self._topo_2numa_with_gpus())
        r = _r(core="0-3,4-7", gpu="0-1")
        plain = pred.score_job(r, 2, 1, 2)
        verbose = pred.score_job_verbose(r, 2, 1, 2)
        self.assertEqual(
            (verbose["satisfied"], verbose["total"]),
            plain,
        )

    def test_verbose_breakdown_one_entry_per_r_lite(self):
        """Each R_lite entry produces one breakdown entry."""
        pred = LocalityPredicate(self._topo_2numa_with_gpus())
        r = {
            "execution": {
                "R_lite": [
                    {"rank": "0", "children": {"core": "0-1", "gpu": "0"}},
                    {"rank": "1", "children": {"core": "4-5", "gpu": "1"}},
                ]
            }
        }
        verbose = pred.score_job_verbose(r, 2, 1, 2)
        self.assertEqual(len(verbose["breakdown"]), 2)

    def test_verbose_lists_unknown_cores(self):
        """Core IDs in R that aren't in the topology surface as
        ``unknown_cores`` — the signature symptom of an ID-scheme
        mismatch between R and the XML."""
        pred = LocalityPredicate(self._topo_2numa_with_gpus())
        r = _r(core="0,1,99")
        verbose = pred.score_job_verbose(r, 1, 0, 3)
        b = verbose["breakdown"][0]
        self.assertEqual(b["unknown_cores"], [99])
        self.assertEqual(sorted(b["cores"]), [0, 1, 99])

    def test_verbose_lists_unknown_gpus(self):
        """Likewise for GPUs."""
        pred = LocalityPredicate(self._topo_2numa_with_gpus())
        r = _r(core="0-3", gpu="42")
        verbose = pred.score_job_verbose(r, 2, 1, 2)
        b = verbose["breakdown"][0]
        self.assertEqual(b["unknown_gpus"], [42])

    def test_verbose_cores_by_domain_distribution(self):
        """The cores-by-domain map shows how the rank's cores
        spread across domains."""
        pred = LocalityPredicate(self._topo_2numa_with_gpus())
        # 3 cores in NUMA 0, 1 in NUMA 1 — the lopsided case.
        r = _r(core="0,1,2,4")
        verbose = pred.score_job_verbose(r, 2, 0, 2)
        b = verbose["breakdown"][0]
        self.assertEqual(b["cores_by_domain"], {"0x1": 3, "0x2": 1})

    def test_verbose_serializable(self):
        """JSON-emit safe — the breakdown can be logged as a
        debug event."""
        import json

        pred = LocalityPredicate(self._topo_2numa_with_gpus())
        r = _r(core="0-3", gpu="0")
        json.dumps(pred.score_job_verbose(r, 2, 1, 2))


class TestLocalityPredicateScoringMatrix(unittest.TestCase):
    """Parametric coverage across diverse topology shapes.

    Each test method describes one scenario (topology + R + slot
    spec + expected score). Adding a regression test for a new
    topology shape means dropping in another method here. The
    failure message includes the case description so a broken
    case is easy to identify in TAP output."""

    def _check(self, topo, r, slot, expected_satisfied, expected_total=None):
        """Build pred from ``topo`` and assert
        ``score_job(r, *slot)`` equals ``(expected_satisfied,
        expected_total or slot[2])``."""
        xml = _build_topo(topo)
        pred = LocalityPredicate(xml)
        if expected_total is None:
            expected_total = slot[2]  # nslots
        actual = pred.score_job(r, *slot)
        self.assertEqual(
            actual,
            (expected_satisfied, expected_total),
            f"score_job({slot}) -> {actual}, "
            f"expected ({expected_satisfied}, {expected_total})",
        )

    # --- Topology shape: single NUMA ---------------------------

    def test_single_numa_perfect_pack(self):
        """1 NUMA, 4 cores, 2-core slots, 2 slots — all local."""
        self._check(
            [{"nodeset": "0x1", "cores": [0, 1, 2, 3]}],
            _r(core="0-3"),
            slot=(2, 0, 2),
            expected_satisfied=2,
        )

    def test_single_numa_slot_larger_than_domain(self):
        """Slot wants more cores than a single domain has —
        zero slots can be satisfied."""
        self._check(
            [{"nodeset": "0x1", "cores": [0, 1]}],
            _r(core="0-1"),
            slot=(4, 0, 1),
            expected_satisfied=0,
        )

    # --- Topology shape: symmetric multi-NUMA ------------------

    def test_dual_numa_balanced_full(self):
        """Two NUMAs, both fully used, slots fit cleanly."""
        self._check(
            [{"nodeset": "0x1", "cores": [0, 1]}, {"nodeset": "0x2", "cores": [2, 3]}],
            _r(core="0-3"),
            slot=(2, 0, 2),
            expected_satisfied=2,
        )

    def test_quad_numa_lopsided_slot_pair(self):
        """3 cores in one NUMA + 1 in another, asking for 2-core
        slots: 3//2=1 satisfied in the first NUMA, 1//2=0 in the
        second."""
        self._check(
            [
                {"nodeset": "0x1", "cores": [0, 1, 2, 3]},
                {"nodeset": "0x2", "cores": [4, 5, 6, 7]},
            ],
            _r(core="0-2,4"),
            slot=(2, 0, 2),
            expected_satisfied=1,
        )

    # --- Topology shape: heterogeneous NUMA sizes --------------

    def test_heterogeneous_numa_sizes(self):
        """One small NUMA (2 cores), one large (8 cores). With a
        4-core slot and 2 slots requested, only the large NUMA
        can hold a slot (2//4=0 and 8//4=2, capped at nslots=2)."""
        self._check(
            [
                {"nodeset": "0x1", "cores": [0, 1]},
                {"nodeset": "0x2", "cores": [2, 3, 4, 5, 6, 7, 8, 9]},
            ],
            _r(core="0-9"),
            slot=(4, 0, 2),
            expected_satisfied=2,
        )

    # --- Topology shape: GPU-sparse ---------------------------

    def test_gpu_sparse_only_one_numa_has_gpu(self):
        """Two NUMAs but only one has a GPU; a 2-core+1-GPU slot
        can be satisfied only in the GPU-bearing NUMA."""
        self._check(
            [
                {"nodeset": "0x1", "cores": [0, 1, 2, 3]},
                {"nodeset": "0x2", "cores": [4, 5, 6, 7], "gpus": ["cuda0"]},
            ],
            _r(core="0-7", gpu="0"),
            slot=(2, 1, 2),
            expected_satisfied=1,
        )

    def test_gpu_sparse_no_satisfying_numa(self):
        """Cores in one NUMA, GPU in another — no slot satisfied."""
        self._check(
            [
                {"nodeset": "0x1", "cores": [0, 1]},
                {"nodeset": "0x2", "gpus": ["cuda0"]},
            ],
            _r(core="0-1", gpu="0"),
            slot=(2, 1, 1),
            expected_satisfied=0,
        )

    def test_gpu_constraint_ignored_when_slot_gpus_zero(self):
        """slot_gpus=0 means GPU presence is irrelevant; the
        same R that fails the GPU-required version succeeds."""
        self._check(
            [
                {"nodeset": "0x1", "cores": [0, 1]},
                {"nodeset": "0x2", "gpus": ["cuda0"]},
            ],
            _r(core="0-1", gpu="0"),
            slot=(2, 0, 1),
            expected_satisfied=1,
        )

    # --- Topology shape: multi-rank R --------------------------

    def test_multi_rank_uniform_placement(self):
        """A single R_lite entry that spans 4 ranks with the
        same children — 4 ranks × 1 satisfied slot/rank = 4
        slots, capped at nslots=4."""
        self._check(
            [
                {"nodeset": "0x1", "cores": [0, 1, 2, 3]},
                {"nodeset": "0x2", "cores": [4, 5, 6, 7]},
            ],
            _r(rank="0-3", core="0-1"),
            slot=(2, 0, 4),
            expected_satisfied=4,
        )

    def test_multi_rank_score_capped_at_nslots(self):
        """If the per-rank count would exceed nslots, the
        return value caps at nslots so the ratio stays in [0, 1]."""
        self._check(
            [{"nodeset": "0x1", "cores": [0, 1, 2, 3]}],
            _r(rank="0-9", core="0-1"),
            # 10 ranks × 1 slot = 10, but nslots=2 caps it.
            slot=(2, 0, 2),
            expected_satisfied=2,
        )

    # --- Edge cases --------------------------------------------

    def test_empty_r_lite_scores_zero(self):
        """An R with no R_lite entries scores zero, but total
        still reflects nslots."""
        self._check(
            [{"nodeset": "0x1", "cores": [0, 1]}],
            {"execution": {"R_lite": []}},
            slot=(1, 0, 4),
            expected_satisfied=0,
        )

    def test_unknown_resources_silently_dropped(self):
        """Cores in R that aren't in the topology are treated as
        not-local rather than raising. (Use score_job_verbose to
        surface the mismatch when this is unexpected.)"""
        self._check(
            [{"nodeset": "0x1", "cores": [0, 1, 2, 3]}],
            _r(core="0,1,2,99"),
            slot=(2, 0, 2),
            # 3 known cores in NUMA 0; 3//2 = 1 slot satisfied.
            expected_satisfied=1,
        )


class TestLocalityPredicateScoring(unittest.TestCase):
    """Per-job scoring: how slots map to satisfied / total."""

    def _topo_4numa(self):
        """16 cores across 4 NUMA domains, 4 cores each."""
        return _build_topo(
            [
                {"nodeset": "0x1", "cores": [0, 1, 2, 3]},
                {"nodeset": "0x2", "cores": [4, 5, 6, 7]},
                {"nodeset": "0x4", "cores": [8, 9, 10, 11]},
                {"nodeset": "0x8", "cores": [12, 13, 14, 15]},
            ]
        )

    def test_all_in_one_domain(self):
        """Job using only NUMA 0's cores scores 1.0 — every slot
        fits in the same domain."""
        pred = LocalityPredicate(self._topo_4numa())
        satisfied, total = pred.score_job(
            _r(core="0-3"),
            slot_cores=2,
            slot_gpus=0,
            nslots=2,
        )
        self.assertEqual((satisfied, total), (2, 2))

    def test_even_split_across_domains(self):
        """Job split with whole slots on each of two domains
        scores 1.0 — every slot still fits in some domain."""
        pred = LocalityPredicate(self._topo_4numa())
        satisfied, total = pred.score_job(
            _r(core="0-1,4-5"),
            slot_cores=2,
            slot_gpus=0,
            nslots=2,
        )
        self.assertEqual((satisfied, total), (2, 2))

    def test_lopsided_split(self):
        """Cores 0,1,2 in NUMA 0 + core 4 in NUMA 1 with
        slot_cores=2: NUMA 0 fits 1 complete slot (3//2=1),
        NUMA 1 fits 0 (1//2=0). Score = 1/2."""
        pred = LocalityPredicate(self._topo_4numa())
        satisfied, total = pred.score_job(
            _r(core="0-2,4"),
            slot_cores=2,
            slot_gpus=0,
            nslots=2,
        )
        self.assertEqual((satisfied, total), (1, 2))

    def test_multi_rank_r_lite(self):
        """An R_lite entry that spans 2 ranks with identical
        allocations counts each rank's satisfied slots."""
        pred = LocalityPredicate(self._topo_4numa())
        # Each of 2 ranks gets cores 0-1: 1 slot satisfied per
        # rank * 2 ranks = 2 satisfied.
        satisfied, total = pred.score_job(
            _r(rank="0-1", core="0-1"),
            slot_cores=2,
            slot_gpus=0,
            nslots=2,
        )
        self.assertEqual((satisfied, total), (2, 2))

    def test_gpu_constraint_satisfied(self):
        """Slot wanting cores + a GPU is satisfied when both
        sit in the same domain."""
        xml = _build_topo(
            [
                {"nodeset": "0x1", "cores": [0, 1], "gpus": ["cuda0"]},
                {"nodeset": "0x2", "cores": [2, 3], "gpus": ["cuda1"]},
            ]
        )
        pred = LocalityPredicate(xml)
        # Each domain has the cores + the GPU for 1 slot of 2c+1g.
        satisfied, total = pred.score_job(
            _r(core="0-3", gpu="0-1"),
            slot_cores=2,
            slot_gpus=1,
            nslots=2,
        )
        self.assertEqual((satisfied, total), (2, 2))

    def test_gpu_constraint_unsatisfied_across_domains(self):
        """Cores on domain A + GPU on domain B can't form a
        slot that requires both. Score = 0."""
        xml = _build_topo(
            [
                {"nodeset": "0x1", "cores": [0, 1]},
                {"nodeset": "0x2", "gpus": ["cuda1"]},
            ]
        )
        pred = LocalityPredicate(xml)
        satisfied, total = pred.score_job(
            _r(core="0-1", gpu="1"),
            slot_cores=2,
            slot_gpus=1,
            nslots=1,
        )
        self.assertEqual((satisfied, total), (0, 1))

    def test_gpu_count_zero_branch(self):
        """slot_gpus=0 means GPU presence is irrelevant; only
        core packing matters."""
        pred = LocalityPredicate(self._topo_4numa())
        satisfied, total = pred.score_job(
            _r(core="0-3"),
            slot_cores=2,
            slot_gpus=0,
            nslots=2,
        )
        self.assertEqual(satisfied, 2)

    def test_score_capped_at_nslots(self):
        """If R has more resources than the job requested
        (shouldn't happen but defensive), the satisfied count
        is capped at ``nslots`` so the ratio is bounded by 1."""
        pred = LocalityPredicate(self._topo_4numa())
        # 16 cores requested, would yield 8 satisfied slots with
        # slot_cores=2, but nslots=2 means score caps at 2.
        satisfied, total = pred.score_job(
            _r(core="0-15"),
            slot_cores=2,
            slot_gpus=0,
            nslots=2,
        )
        self.assertEqual(satisfied, 2)
        self.assertEqual(total, 2)

    def test_empty_r_lite(self):
        """A job R with no R_lite entries scores (0, nslots)."""
        pred = LocalityPredicate(self._topo_4numa())
        satisfied, total = pred.score_job(
            {"execution": {"R_lite": []}},
            slot_cores=1,
            slot_gpus=0,
            nslots=4,
        )
        self.assertEqual((satisfied, total), (0, 4))

    def test_unknown_resources_silently_dropped(self):
        """Cores in R that aren't in the topology are treated
        as if they don't exist — they neither contribute to
        nor break a slot. This means a mismatch between the
        hwloc XML and the actual R will show as missing
        locality rather than a hard error."""
        pred = LocalityPredicate(self._topo_4numa())
        # Core 99 is not in the topology; only 0-1 count.
        satisfied, total = pred.score_job(
            _r(core="0-1,99"),
            slot_cores=2,
            slot_gpus=0,
            nslots=1,
        )
        self.assertEqual((satisfied, total), (1, 1))


class TestLocalityPredicateOverride(unittest.TestCase):
    """``_domain_of`` is the documented extension point — verify
    that overriding it actually changes the scoring outcome."""

    def test_override_uses_cpuset_instead_of_nodeset(self):
        """A subclass keying off ``cpuset`` rather than
        ``nodeset`` produces different domain groupings, hence
        different scores."""
        xml = ET.tostring(
            ET.fromstring(
                """\
<topology version="2.0">
  <object type="Machine">
    <object type="Package" cpuset="0x000f">
      <object type="Core" os_index="0" cpuset="0x0001" nodeset="0x1"/>
      <object type="Core" os_index="1" cpuset="0x0002" nodeset="0x2"/>
    </object>
    <object type="Package" cpuset="0x00f0">
      <object type="Core" os_index="2" cpuset="0x0010" nodeset="0x4"/>
    </object>
  </object>
</topology>"""
            ),
            encoding="unicode",
        )

        class PackagePredicate(LocalityPredicate):
            def _domain_of(self, obj, ancestor_chain):
                # Walk up looking for a Package's cpuset rather
                # than any nodeset.
                for ancestor in reversed(ancestor_chain):
                    if ancestor.get("type") == "Package":
                        return ancestor.get("cpuset")
                return None

        pred = PackagePredicate(xml)
        # Cores 0 and 1 have different NUMAs but share Package 0;
        # the override puts them in one domain.
        self.assertEqual(pred.core_domain[0], pred.core_domain[1])
        self.assertNotEqual(pred.core_domain[1], pred.core_domain[2])


# ---------------------------------------------------------------
# Module-level helpers
# ---------------------------------------------------------------


class TestParseDurationMode(unittest.TestCase):

    def test_fixed_fsd(self):
        self.assertEqual(
            _parse_duration_mode("0.1s"),
            ("fixed", "0.1s", "0.1s"),
        )

    def test_fill_keyword(self):
        self.assertEqual(_parse_duration_mode("fill"), ("fill", None, None))

    def test_random_default_range(self):
        self.assertEqual(
            _parse_duration_mode("random"),
            ("random", "0.1s", "1.0s"),
        )

    def test_random_explicit_range(self):
        self.assertEqual(
            _parse_duration_mode("random:0.5s-2s"),
            ("random", "0.5s", "2s"),
        )

    def test_random_missing_hyphen_raises(self):
        with self.assertRaises(ValueError):
            _parse_duration_mode("random:0.5s")

    def test_invalid_fsd_raises(self):
        # parse_fsd raises on garbage; we propagate it as ValueError.
        with self.assertRaises(ValueError):
            _parse_duration_mode("not-a-duration")


class TestParseIdsetString(unittest.TestCase):

    def test_empty_returns_empty_list(self):
        self.assertEqual(_parse_idset_string(""), [])

    def test_single_value(self):
        self.assertEqual(_parse_idset_string("5"), [5])

    def test_range(self):
        self.assertEqual(_parse_idset_string("0-3"), [0, 1, 2, 3])

    def test_mixed_list_and_ranges(self):
        self.assertEqual(
            _parse_idset_string("0-2,5,7-8"),
            [0, 1, 2, 5, 7, 8],
        )


class TestRFromEvent(unittest.TestCase):
    """:func:`_r_from_event` reads R off the journal alloc event's
    ``R`` property; absent means caller falls back to KVS."""

    def test_returns_R_attribute(self):
        class FakeEvent:
            R = {"execution": {"R_lite": [{"rank": "0"}]}}

        self.assertEqual(
            _r_from_event(FakeEvent()),
            {"execution": {"R_lite": [{"rank": "0"}]}},
        )

    def test_returns_none_when_absent(self):
        class FakeEvent:
            name = "alloc"

        self.assertIsNone(_r_from_event(FakeEvent()))


if __name__ == "__main__":
    from pycotap import TAPTestRunner

    unittest.main(testRunner=TAPTestRunner())

# vi: ts=4 sw=4 expandtab
