#!/usr/bin/env python3
##############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

"""Unit tests for flux.testing.fake_resources.

Pure-Python tests of the slot-math helper, the argv builder, the amend_R hook
contract, and the ABC's abstract requirement. None of these tests require a
running broker.

End-to-end behavior (broker actually starts with synthetic R, resource module
accepts it, scheduler allocates against it, amend_R takes effect) is covered
by the sharness test t6010-fake-resources.t.
"""

import os
import tempfile
import unittest

import subflux  # noqa: F401 — side-effect import wires PYTHONPATH
from flux.testing.fake_resources import (
    FakeResources,
    InjectFakeResources,
    load_amender,
    saturation_count,
)


class TestSaturationCount(unittest.TestCase):
    """Module-level saturation_count() is pure arithmetic; verify
    the math against known shapes."""

    def test_cores_only(self):
        """nnodes=10, 64 cores/node, slot=1 core: 640 slots."""
        self.assertEqual(saturation_count(10, 64, 0, slot_cores=1), 640)

    def test_cores_wider_slot(self):
        """nnodes=10, 64 cores/node, slot=5 cores: 120 slots
        (per_node = 64 // 5 = 12, total = 10 * 12 = 120)."""
        self.assertEqual(saturation_count(10, 64, 0, slot_cores=5), 120)

    def test_cores_and_gpus_gpu_bound(self):
        """nnodes=10, 64c/8g, slot=1c/1g: 80 slots (GPU-bound;
        per_node = min(64, 8) = 8)."""
        self.assertEqual(
            saturation_count(10, 64, 8, slot_cores=1, slot_gpus=1),
            80,
        )

    def test_cores_and_gpus_cpu_bound(self):
        """nnodes=4, 4c/8g, slot=2c/1g: 8 slots (CPU-bound;
        per_node = min(4//2, 8//1) = min(2, 8) = 2)."""
        self.assertEqual(
            saturation_count(4, 4, 8, slot_cores=2, slot_gpus=1),
            8,
        )

    def test_single_node(self):
        """nnodes=1 still works (degenerate but valid)."""
        self.assertEqual(saturation_count(1, 8, 0, slot_cores=1), 8)

    def test_zero_gpu_node_with_zero_gpu_slot(self):
        """No GPUs requested and none available: ignore the GPU
        dimension entirely, fall back to core-only math."""
        self.assertEqual(
            saturation_count(4, 8, 0, slot_cores=1, slot_gpus=0),
            32,
        )

    def test_slot_zero_zero_raises(self):
        """slot_cores=0 and slot_gpus=0 is meaningless — a slot
        that requests nothing — and the function should reject it."""
        with self.assertRaises(ValueError):
            saturation_count(10, 64, 8, slot_cores=0, slot_gpus=0)

    def test_slot_gpu_only_with_no_node_gpus(self):
        """Requesting GPU slots on a GPU-less cluster: 0 slots
        (per_node = min(cores, 0) = 0)."""
        self.assertEqual(
            saturation_count(10, 64, 0, slot_cores=0, slot_gpus=1),
            0,
        )

    def test_method_form_matches_function_form(self):
        """FakeResources.saturation_count() is a thin wrapper around
        the module-level function and must produce identical results."""
        fr = InjectFakeResources(nodes=10, cores_per_node=64, gpus_per_node=8)
        for slot_cores, slot_gpus in [(1, 0), (5, 0), (1, 1), (2, 1), (4, 2)]:
            with self.subTest(slot_cores=slot_cores, slot_gpus=slot_gpus):
                method = fr.saturation_count(
                    slot_cores=slot_cores,
                    slot_gpus=slot_gpus,
                )
                func = saturation_count(
                    fr.nodes,
                    fr.cores_per_node,
                    fr.gpus_per_node,
                    slot_cores=slot_cores,
                    slot_gpus=slot_gpus,
                )
                self.assertEqual(method, func)


class TestInjectFakeResourcesInit(unittest.TestCase):
    """Constructor stores arguments on the instance for use by
    install() and downstream consumers."""

    def test_required_argument_is_nodes(self):
        """``nodes`` is the only positional required arg."""
        fr = InjectFakeResources(nodes=4)
        self.assertEqual(fr.nodes, 4)

    def test_defaults_match_class_contract(self):
        """Defaults: 1 core/node, 0 gpus/node, host prefix 'fake',
        no hwloc XML path, no verbose, default log."""
        fr = InjectFakeResources(nodes=4)
        self.assertEqual(fr.cores_per_node, 1)
        self.assertEqual(fr.gpus_per_node, 0)
        self.assertEqual(fr.host_prefix, "fake")
        self.assertIsNone(fr.hwloc_xml_path)
        self.assertFalse(fr.verbose)
        self.assertIsNotNone(fr.log)

    def test_total_properties(self):
        """total_cores / total_gpus are computed properties."""
        fr = InjectFakeResources(nodes=4, cores_per_node=8, gpus_per_node=2)
        self.assertEqual(fr.total_cores, 32)
        self.assertEqual(fr.total_gpus, 8)

    def test_log_callable_override(self):
        """A user-supplied log callable replaces the default."""
        captured = []
        fr = InjectFakeResources(nodes=4, log=captured.append)
        fr.log("hello")
        self.assertEqual(captured, ["hello"])

    def test_hwloc_xml_path_stored_verbatim(self):
        """The path is stored as-is; the file is opened later by
        _encode_R(). Parse errors and missing files surface at install() time,
        not construction time."""
        fr = InjectFakeResources(nodes=1, hwloc_xml_path="/path/to/topo.xml")
        self.assertEqual(fr.hwloc_xml_path, "/path/to/topo.xml")


class TestAmendRHook(unittest.TestCase):
    """The amend_R hook lets subclasses inject metadata into R
    before it's written to KVS. Verify default is a no-op and that override
    semantics work as expected."""

    def test_default_is_noop(self):
        """Base class amend_R returns R unchanged."""
        fr = InjectFakeResources(nodes=4)
        R = {"version": 1, "execution": {"R_lite": []}}
        result = fr.amend_R(R)
        self.assertEqual(result, R)

    def test_default_passes_through_xml(self):
        """When hwloc_xml is provided, the base class still returns
        R unchanged. The XML is given to the hook in case a subclass wants to
        consult it, but the default doesn't."""
        fr = InjectFakeResources(nodes=4)
        R = {"version": 1}
        result = fr.amend_R(R, hwloc_xml="<topology></topology>")
        self.assertEqual(result, R)

    def test_subclass_override_modifies_R(self):
        """A subclass can return a modified R."""

        class WithProperties(InjectFakeResources):
            def amend_R(self, R, hwloc_xml=None):
                R["properties"] = {"bigmem": "0-3"}
                return R

        fr = WithProperties(nodes=4)
        R = {"version": 1, "execution": {"R_lite": []}}
        result = fr.amend_R(R)
        self.assertIn("properties", result)
        self.assertEqual(result["properties"], {"bigmem": "0-3"})

    def test_subclass_sees_xml(self):
        """A subclass that wants to consult the original XML
        topology gets it via the hwloc_xml parameter."""
        seen_xml = []

        class XmlAware(InjectFakeResources):
            def amend_R(self, R, hwloc_xml=None):
                seen_xml.append(hwloc_xml)
                return R

        xml = "<topology><object type='Machine'/></topology>"
        fr = XmlAware(nodes=1)
        fr.amend_R({}, hwloc_xml=xml)
        self.assertEqual(seen_xml, [xml])


class TestConstructorAmender(unittest.TestCase):
    """The ``amender=`` constructor parameter lets the default
    amend_R() delegate to an injected callable, used by the fake-resources
    modprobe rc1 task to support the ``amend-r`` TOML key without forcing a
    subclass."""

    def test_amender_defaults_to_None(self):
        """Without an amender, the attribute is None and amend_R
        is a no-op."""
        fr = InjectFakeResources(nodes=4)
        self.assertIsNone(fr.amender)
        R = {"version": 1}
        self.assertEqual(fr.amend_R(R), R)

    def test_amender_callable_stored(self):
        """The amender callable is stored on the instance verbatim."""

        def my_amender(R, hwloc_xml=None):
            return R

        fr = InjectFakeResources(nodes=4, amender=my_amender)
        self.assertIs(fr.amender, my_amender)

    def test_amender_string_resolved_at_construction(self):
        """A string amender argument is resolved via load_amender at
        construction time; the resulting callable is stored. This
        lets the rc1 task pass the TOML value directly without first
        calling load_amender itself."""
        amender = InjectFakeResources(
            nodes=4,
            amender="os.path:basename",
        ).amender
        self.assertIs(amender, os.path.basename)

    def test_amender_invalid_string_raises_at_construction(self):
        """A malformed string amender raises immediately, not later
        at amend_R() time; the failure surfaces close to the source
        of the bad value (the TOML config) rather than deep in the
        install pipeline."""
        with self.assertRaises(RuntimeError):
            InjectFakeResources(nodes=4, amender="no_such_module_xyz:fn")

    def test_default_amend_R_delegates_to_amender(self):
        """When amender is set, the default amend_R defers to it
        and returns whatever the amender returns."""

        def my_amender(R, hwloc_xml=None):
            R["from_amender"] = True
            return R

        fr = InjectFakeResources(nodes=4, amender=my_amender)
        result = fr.amend_R({"original": True})
        self.assertEqual(
            result,
            {"original": True, "from_amender": True},
        )

    def test_amender_receives_hwloc_xml_kwarg(self):
        """The hwloc_xml argument is passed through to the amender."""
        seen = []

        def my_amender(R, hwloc_xml=None):
            seen.append(hwloc_xml)
            return R

        fr = InjectFakeResources(nodes=4, amender=my_amender)
        xml = "<topology></topology>"
        fr.amend_R({}, hwloc_xml=xml)
        self.assertEqual(seen, [xml])

    def test_subclass_amend_R_overrides_amender(self):
        """A subclass that overrides amend_R takes precedence over
        the constructor amender — subclass overrides win, and a subclass that
        wants to incorporate self.amender should call super().amend_R()
        explicitly."""
        amender_called = []

        def my_amender(R, hwloc_xml=None):
            amender_called.append(True)
            R["amender"] = True
            return R

        class SubOverride(InjectFakeResources):
            def amend_R(self, R, hwloc_xml=None):
                R["subclass"] = True
                return R

        fr = SubOverride(nodes=4, amender=my_amender)
        result = fr.amend_R({})
        self.assertEqual(result, {"subclass": True})
        self.assertEqual(amender_called, [])


class TestLoadAmender(unittest.TestCase):
    """load_amender() resolves a TOML amend-r spec to a callable.
    Two forms: ``module:function`` and a filesystem path with fallback to
    ``amend`` at module scope. Resolution errors raise RuntimeError with
    descriptive messages so the rc1 task can surface configuration mistakes
    clearly."""

    def test_module_function_form_resolves(self):
        """`module:function` returns the named attribute of the
        imported module. Uses os.path.basename as a stable stdlib callable —
        we only verify the lookup mechanism, not the callable's signature
        (which doesn't match amend_R)."""
        amender = load_amender("os.path:basename")
        self.assertIs(amender, os.path.basename)

    def test_module_function_missing_module_raises(self):
        """Importing a non-existent module produces a clear
        RuntimeError, not a leaked ImportError."""
        with self.assertRaises(RuntimeError) as ctx:
            load_amender("no_such_module_xyz:fn")
        msg = str(ctx.exception)
        self.assertIn("amend-r", msg)
        self.assertIn("no_such_module_xyz", msg)

    def test_module_function_missing_attribute_raises(self):
        """A real module without the named attribute also raises."""
        with self.assertRaises(RuntimeError) as ctx:
            load_amender("os.path:no_such_function_xyz")
        msg = str(ctx.exception)
        self.assertIn("amend-r", msg)
        self.assertIn("no_such_function_xyz", msg)

    def test_file_path_form_resolves(self):
        """A spec without ':' is loaded as a file; the file's
        ``amend`` callable is returned."""
        with tempfile.NamedTemporaryFile(
            mode="w",
            suffix=".py",
            delete=False,
        ) as f:
            f.write(
                "def amend(R, hwloc_xml=None):\n"
                "    R['from_file'] = True\n"
                "    return R\n"
            )
            path = f.name
        try:
            amender = load_amender(path)
            result = amender({}, hwloc_xml=None)
            self.assertEqual(result, {"from_file": True})
        finally:
            os.unlink(path)

    def test_file_path_missing_file_raises(self):
        """A path that doesn't exist raises with a clear message."""
        with self.assertRaises(RuntimeError) as ctx:
            load_amender("/no/such/path/amender.py")
        msg = str(ctx.exception).lower()
        self.assertIn("amend-r", msg)
        self.assertIn("not found", msg)

    def test_file_without_amend_callable_raises(self):
        """A file that loads but has no ``amend`` attribute raises."""
        with tempfile.NamedTemporaryFile(
            mode="w",
            suffix=".py",
            delete=False,
        ) as f:
            f.write("def not_amend(R, hwloc_xml=None):\n" "    return R\n")
            path = f.name
        try:
            with self.assertRaises(RuntimeError) as ctx:
                load_amender(path)
            msg = str(ctx.exception)
            self.assertIn("amend-r", msg)
            self.assertIn("amend", msg)
        finally:
            os.unlink(path)

    def test_file_with_syntax_error_raises(self):
        """A malformed Python file produces a clean error rather
        than a bare SyntaxError leaking up."""
        with tempfile.NamedTemporaryFile(
            mode="w",
            suffix=".py",
            delete=False,
        ) as f:
            f.write("def amend(R, hwloc_xml=None\n    return R\n")
            path = f.name
        try:
            with self.assertRaises(RuntimeError) as ctx:
                load_amender(path)
            self.assertIn("amend-r", str(ctx.exception))
        finally:
            os.unlink(path)


class TestFakeResourcesIsAbstract(unittest.TestCase):
    """FakeResources is an ABC; install() is abstract and must be
    implemented by subclasses."""

    def test_cannot_instantiate_directly(self):
        with self.assertRaises(TypeError):
            FakeResources(nodes=4)

    def test_concrete_subclass_works(self):
        """A trivial subclass that implements install can be
        instantiated and inherits properties + amend_R default."""

        class _NoOpFR(FakeResources):
            def install(self):
                pass

        fr = _NoOpFR(nodes=4, cores_per_node=8, gpus_per_node=2)
        self.assertEqual(fr.total_cores, 32)
        self.assertEqual(fr.total_gpus, 8)
        # Default amend_R is a no-op
        R = {"version": 1}
        self.assertEqual(fr.amend_R(R), R)


if __name__ == "__main__":
    from pycotap import TAPTestRunner

    unittest.main(testRunner=TAPTestRunner(), buffer=False)
