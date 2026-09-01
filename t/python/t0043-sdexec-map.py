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

import errno
import io
import json
import math
import os
import re
import stat as stat_mod
import tempfile
import unittest
from pathlib import Path
from unittest import mock
from unittest.mock import patch

import flux.sdexec.map as m
import subflux  # noqa: F401 - for PYTHONPATH
from flux.idset import IDset
from flux.sdexec.map import HwlocMapper, ResourceMapper, merge_allowed_devices
from pycotap import TAPTestRunner

os.environ["PATH"] = (
    os.path.dirname(subflux.flux_exe) + os.pathsep + os.environ.get("PATH", "")
)


def make_R(rank=0, cores=None, gpus=None):
    """Build a minimal R JSON string for the given rank and resource IDs."""
    children = {}
    if cores is not None:
        children["core"] = cores
    if gpus is not None:
        children["gpu"] = gpus
    return json.dumps(
        {
            "version": 1,
            "execution": {
                "R_lite": [{"rank": str(rank), "children": children}],
                "nodelist": [f"node{rank}"],
            },
        }
    )


#  Minimal x86 topology: 1 NUMA node, 1 socket, 2 cores (4 PUs),
#  1 PCI bridge with 2 CUDA GPU children at known PCI addresses.
#
#  Core 0: PUs 0,1  -> cpus "0-1"
#  Core 1: PUs 2,3  -> cpus "2-3"
#  Both cores in NUMA node 0 -> mems "0"
#  GPU 0: PCI 0000:01:00.0
#  GPU 1: PCI 0000:02:00.0
HWLOC_XML = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE topology SYSTEM "hwloc.dtd">
<topology>
  <object type="Machine" os_index="0"\
 cpuset="0x0000000f" complete_cpuset="0x0000000f"\
 online_cpuset="0x0000000f" allowed_cpuset="0x0000000f"\
 nodeset="0x00000001" complete_nodeset="0x00000001"\
 allowed_nodeset="0x00000001">
    <object type="NUMANode" os_index="0"\
 cpuset="0x0000000f" complete_cpuset="0x0000000f"\
 online_cpuset="0x0000000f" allowed_cpuset="0x0000000f"\
 nodeset="0x00000001" complete_nodeset="0x00000001"\
 allowed_nodeset="0x00000001" local_memory="8589934592">
      <object type="Package" os_index="0"\
 cpuset="0x0000000f" complete_cpuset="0x0000000f"\
 online_cpuset="0x0000000f" allowed_cpuset="0x0000000f"\
 nodeset="0x00000001" complete_nodeset="0x00000001"\
 allowed_nodeset="0x00000001">
        <object type="Core" os_index="0"\
 cpuset="0x00000003" complete_cpuset="0x00000003"\
 online_cpuset="0x00000003" allowed_cpuset="0x00000003"\
 nodeset="0x00000001" complete_nodeset="0x00000001"\
 allowed_nodeset="0x00000001">
          <object type="PU" os_index="0"\
 cpuset="0x00000001" complete_cpuset="0x00000001"\
 online_cpuset="0x00000001" allowed_cpuset="0x00000001"\
 nodeset="0x00000001" complete_nodeset="0x00000001"\
 allowed_nodeset="0x00000001"/>
          <object type="PU" os_index="1"\
 cpuset="0x00000002" complete_cpuset="0x00000002"\
 online_cpuset="0x00000002" allowed_cpuset="0x00000002"\
 nodeset="0x00000001" complete_nodeset="0x00000001"\
 allowed_nodeset="0x00000001"/>
        </object>
        <object type="Core" os_index="1"\
 cpuset="0x0000000c" complete_cpuset="0x0000000c"\
 online_cpuset="0x0000000c" allowed_cpuset="0x0000000c"\
 nodeset="0x00000001" complete_nodeset="0x00000001"\
 allowed_nodeset="0x00000001">
          <object type="PU" os_index="2"\
 cpuset="0x00000004" complete_cpuset="0x00000004"\
 online_cpuset="0x00000004" allowed_cpuset="0x00000004"\
 nodeset="0x00000001" complete_nodeset="0x00000001"\
 allowed_nodeset="0x00000001"/>
          <object type="PU" os_index="3"\
 cpuset="0x00000008" complete_cpuset="0x00000008"\
 online_cpuset="0x00000008" allowed_cpuset="0x00000008"\
 nodeset="0x00000001" complete_nodeset="0x00000001"\
 allowed_nodeset="0x00000001"/>
        </object>
      </object>
    </object>
    <object type="Bridge" os_index="0" bridge_type="0-1" depth="0"\
 bridge_pci="0000:[00-02]">
      <object type="PCIDev" os_index="4096" name="Test GPU 0"\
 pci_busid="0000:01:00.0" pci_type="0302 [10de:1234] [10de:0000] a1"\
 pci_link_speed="0.000000">
        <object type="OSDev" name="cuda0" osdev_type="5">
          <info name="CoProcType" value="CUDA"/>
          <info name="Backend" value="CUDA"/>
        </object>
      </object>
      <object type="PCIDev" os_index="8192" name="Test GPU 1"\
 pci_busid="0000:02:00.0" pci_type="0302 [10de:1234] [10de:0000] a1"\
 pci_link_speed="0.000000">
        <object type="OSDev" name="cuda1" osdev_type="5">
          <info name="CoProcType" value="CUDA"/>
          <info name="Backend" value="CUDA"/>
        </object>
      </object>
    </object>
  </object>
</topology>
"""


class TestResourceMapper(unittest.TestCase):
    """Test the ResourceMapper base class dispatch mechanism."""

    def setUp(self):
        class EchoMapper(ResourceMapper):
            def map_cores(self, cores):
                return {"cores_got": cores}

            def map_gpus(self, gpus):
                return {"gpus_got": gpus}

        self.mapper = EchoMapper()

    def test_map_dispatches(self):
        result = self.mapper.map(make_R(cores="0-1", gpus="0-1"))
        self.assertEqual(result["cores_got"], "0-1")
        self.assertEqual(result["gpus_got"], "0-1")

    def test_map_unhandled_type_skipped(self):
        # Mapper without map_gpus: gpus present in R are silently skipped.
        class CoresOnlyMapper(ResourceMapper):
            def map_cores(self, cores):
                return {"cores_got": cores}

        mapper = CoresOnlyMapper()
        result = mapper.map(make_R(cores="0", gpus="0"))
        self.assertIn("cores_got", result)
        self.assertNotIn("gpus_got", result)

    def test_map_empty(self):
        # Rank 0 not present in R → empty result.
        self.assertEqual(self.mapper.map(make_R(rank=1, cores="0")), {})


class TestHwlocMapper(unittest.TestCase):
    """Test HwlocMapper with a minimal hwloc XML fixture."""

    def setUp(self):
        self.mapper = HwlocMapper(HWLOC_XML)

    def tearDown(self):
        del self.mapper

    def test_map_cores_single(self):
        result = self.mapper.map_cores("0")
        self.assertEqual(result["AllowedCPUs"], "0-1")
        self.assertEqual(result["AllowedMemoryNodes"], "0")

    def test_map_cores_other(self):
        result = self.mapper.map_cores("1")
        self.assertEqual(result["AllowedCPUs"], "2-3")
        self.assertEqual(result["AllowedMemoryNodes"], "0")

    def test_map_cores_all(self):
        result = self.mapper.map_cores("0-1")
        self.assertEqual(result["AllowedCPUs"], "0-3")
        self.assertEqual(result["AllowedMemoryNodes"], "0")

    def test_map_gpus_pci_addrs(self):
        # Patch sysfs lookup so the test does not require real GPU hardware.
        fake_devs = {"0000:01:00.0": ["/dev/dri/renderD128 rw"]}
        with patch.object(
            self.mapper,
            "_discover_gpu_devices",
            side_effect=lambda addr: fake_devs.get(addr, []),
        ):
            result = self.mapper.map_gpus("0")
        self.assertEqual(result["DeviceAllow"], "/dev/dri/renderD128 rw")

    def test_map_gpus_multiple(self):
        fake_devs = {
            "0000:01:00.0": ["/dev/dri/renderD128 rw"],
            "0000:02:00.0": ["/dev/dri/renderD129 rw"],
        }
        with patch.object(
            self.mapper,
            "_discover_gpu_devices",
            side_effect=lambda addr: fake_devs.get(addr, []),
        ):
            result = self.mapper.map_gpus("0-1")
        self.assertEqual(
            result["DeviceAllow"],
            "/dev/dri/renderD128 rw,/dev/dri/renderD129 rw",
        )

    def test_map_dispatch(self):
        fake_devs = {"0000:01:00.0": ["/dev/dri/renderD128 rw"]}
        with patch.object(
            self.mapper,
            "_discover_gpu_devices",
            side_effect=lambda addr: fake_devs.get(addr, []),
        ):
            result = self.mapper.map(make_R(cores="0", gpus="0"))
        self.assertEqual(result["AllowedCPUs"], "0-1")
        self.assertEqual(result["AllowedMemoryNodes"], "0")
        self.assertEqual(result["DeviceAllow"], "/dev/dri/renderD128 rw")
        self.assertEqual(result["DevicePolicy"], "closed")

    def test_map_no_gpus_closed_policy(self):
        # Jobs with no GPUs must get DevicePolicy=closed to enforce containment.
        result = self.mapper.map(make_R(cores="0"))
        self.assertEqual(result.get("DevicePolicy"), "closed")

    def test_create_bad_xml(self):
        with self.assertRaises(OSError):
            HwlocMapper("not xml")

    def test_map_gpus_nvidia_devices(self):
        """Test NVIDIA-specific device discovery."""
        # Mock NVIDIA devices
        fake_devs = {
            "0000:01:00.0": [
                "/dev/nvidia0 rw",
                "/dev/nvidiactl rw",
                "/dev/nvidia-uvm rw",
                "/dev/dri/renderD128 rw",
            ]
        }
        with patch.object(
            self.mapper,
            "_discover_gpu_devices",
            side_effect=lambda addr: fake_devs.get(addr, []),
        ):
            result = self.mapper.map_gpus("0")
        devices = result["DeviceAllow"]
        self.assertIn("/dev/nvidia0 rw", devices)
        self.assertIn("/dev/nvidiactl rw", devices)
        self.assertIn("/dev/dri/renderD128 rw", devices)

    def test_map_gpus_amd_devices(self):
        """Test AMD-specific device discovery including KFD."""
        # Mock AMD devices
        fake_devs = {
            "0000:01:00.0": [
                "/dev/dri/renderD128 rw",
                "/dev/dri/card0 rw",
                "/dev/kfd rw",
            ]
        }
        with patch.object(
            self.mapper,
            "_discover_gpu_devices",
            side_effect=lambda addr: fake_devs.get(addr, []),
        ):
            result = self.mapper.map_gpus("0")
        devices = result["DeviceAllow"]
        self.assertIn("/dev/kfd rw", devices)
        self.assertIn("/dev/dri/renderD128 rw", devices)

    def test_map_gpus_deduplicates_shared_devices(self):
        """Test that shared devices (kfd, nvidiactl) aren't duplicated."""
        # Mock two AMD GPUs that both add /dev/kfd
        fake_devs = {
            "0000:01:00.0": ["/dev/dri/renderD128 rw", "/dev/kfd rw"],
            "0000:02:00.0": ["/dev/dri/renderD129 rw", "/dev/kfd rw"],
        }
        with patch.object(
            self.mapper,
            "_discover_gpu_devices",
            side_effect=lambda addr: fake_devs.get(addr, []),
        ):
            result = self.mapper.map_gpus("0-1")
        devices_list = result["DeviceAllow"].split(",")
        kfd_count = sum(1 for d in devices_list if "kfd" in d)
        self.assertEqual(kfd_count, 1, "/dev/kfd should only appear once")


class TestDiscoverNvidiaErrors(unittest.TestCase):
    """Test that _discover_nvidia_devices raises OSError on missing devices."""

    def setUp(self):
        self.mapper = HwlocMapper(HWLOC_XML)
        self.pci_path = Path("/sys/bus/pci/devices/0000:01:00.0")

    def tearDown(self):
        del self.mapper

    def test_proc_info_absent_raises_enodev(self):
        """ENODEV raised when /proc/driver/nvidia info file does not exist."""
        with patch.object(Path, "exists", lambda self: False):
            with self.assertRaises(OSError) as ctx:
                self.mapper._discover_nvidia_devices(self.pci_path)
        self.assertEqual(ctx.exception.errno, errno.ENODEV)

    def test_nvidia_dev_absent_raises_enodev(self):
        """ENODEV raised when Device Minor is found but /dev/nvidia<N> is absent."""
        info_text = "Model: Test GPU\nDevice Minor: 0\n"

        def exists(p):
            return "information" in str(p)  # info_file present, /dev/nvidiaN absent

        with patch.object(Path, "exists", exists):
            with patch.object(Path, "read_text", lambda p, **kw: info_text):
                with self.assertRaises(OSError) as ctx:
                    self.mapper._discover_nvidia_devices(self.pci_path)
        self.assertEqual(ctx.exception.errno, errno.ENODEV)


class TestDiscoverAmdErrors(unittest.TestCase):
    """Test that AMD device discovery raises OSError on missing devices."""

    def setUp(self):
        self.mapper = HwlocMapper(HWLOC_XML)
        self.pci_path = Path("/sys/bus/pci/devices/0000:01:00.0")

    def tearDown(self):
        del self.mapper

    def test_kfd_absent_raises_enodev(self):
        """ENODEV raised when /dev/kfd does not exist."""
        with patch.object(Path, "exists", lambda self: False):
            with self.assertRaises(OSError) as ctx:
                self.mapper._discover_amd_devices(self.pci_path)
        self.assertEqual(ctx.exception.errno, errno.ENODEV)

    def test_no_dri_devices_raises_enodev(self):
        """ENODEV raised when no /dev/dri devices are found for an AMD GPU."""
        with patch.object(self.mapper, "_discover_drm_devices", return_value=[]):
            with patch.object(self.mapper, "_get_driver_name", return_value="amdgpu"):
                with self.assertRaises(OSError) as ctx:
                    self.mapper._discover_gpu_devices("0000:01:00.0")
        self.assertEqual(ctx.exception.errno, errno.ENODEV)


class TestCustomGpuMapper(unittest.TestCase):
    """Subclass overriding map_gpus; cores mapping is inherited from HwlocMapper."""

    def setUp(self):
        class NvidiaMapper(HwlocMapper):
            def map_gpus(self, gpus):
                if not gpus:
                    return {}
                return {
                    "DeviceAllow": ",".join(f"/dev/nvidia{g} rw" for g in IDset(gpus))
                }

        self.mapper = NvidiaMapper(HWLOC_XML)

    def test_cores_inherited(self):
        result = self.mapper.map_cores("0")
        self.assertEqual(result["AllowedCPUs"], "0-1")
        self.assertEqual(result["AllowedMemoryNodes"], "0")

    def test_custom_gpus(self):
        result = self.mapper.map_gpus("0-1")
        self.assertEqual(result["DeviceAllow"], "/dev/nvidia0 rw,/dev/nvidia1 rw")

    def test_map_uses_custom_gpus(self):
        result = self.mapper.map(make_R(cores="1", gpus="0-1"))
        self.assertEqual(result["AllowedCPUs"], "2-3")
        self.assertIn("/dev/nvidia0 rw", result["DeviceAllow"])
        self.assertEqual(result["DevicePolicy"], "closed")


class TestCustomCoresMapper(unittest.TestCase):
    """Subclass overriding map_cores; GPU mapping is inherited from HwlocMapper."""

    def setUp(self):
        class IsolatedCpuMapper(HwlocMapper):
            """Override map_cores to pin to a single fixed CPU set."""

            def map_cores(self, cores):
                return {"AllowedCPUs": "0", "AllowedMemoryNodes": "0"}

        self.mapper = IsolatedCpuMapper(HWLOC_XML)

    def test_custom_cores(self):
        result = self.mapper.map_cores("0-1")
        self.assertEqual(result["AllowedCPUs"], "0")
        self.assertEqual(result["AllowedMemoryNodes"], "0")

    def test_gpus_inherited(self):
        fake_devs = {"0000:01:00.0": ["/dev/dri/renderD128 rw"]}
        with patch.object(
            self.mapper,
            "_discover_gpu_devices",
            side_effect=lambda addr: fake_devs.get(addr, []),
        ):
            result = self.mapper.map_gpus("0")
        self.assertEqual(result["DeviceAllow"], "/dev/dri/renderD128 rw")

    def test_map_uses_custom_cores(self):
        result = self.mapper.map(make_R(cores="0-1"))
        self.assertEqual(result["AllowedCPUs"], "0")


class TestFinalizeProperties(unittest.TestCase):
    """Test the finalize_properties extension hook."""

    def test_add_properties(self):
        """Test mapper that adds new properties."""

        class AccountingMapper(HwlocMapper):
            def finalize_properties(self, properties, R, extra_properties=None):
                properties.update(
                    {
                        "CPUAccounting": "true",
                        "MemoryAccounting": "true",
                    }
                )
                return super().finalize_properties(
                    properties, R, extra_properties=extra_properties
                )

        mapper = AccountingMapper(HWLOC_XML)
        result = mapper.map(make_R(cores="0"))

        # Original properties preserved
        self.assertEqual(result["AllowedCPUs"], "0-1")
        self.assertEqual(result["AllowedMemoryNodes"], "0")

        # New properties added
        self.assertEqual(result["CPUAccounting"], "true")
        self.assertEqual(result["MemoryAccounting"], "true")
        self.assertEqual(result["DevicePolicy"], "closed")

    def test_modify_properties(self):
        """Test mapper that modifies existing properties."""

        class OverrideMapper(HwlocMapper):
            def finalize_properties(self, properties, R, extra_properties=None):
                # Override CPU allocation to a fixed value
                properties["AllowedCPUs"] = "0"
                return super().finalize_properties(
                    properties, R, extra_properties=extra_properties
                )

        mapper = OverrideMapper(HWLOC_XML)
        result = mapper.map(make_R(cores="0-1"))

        # Property was overridden
        self.assertEqual(result["AllowedCPUs"], "0")
        self.assertEqual(result["DevicePolicy"], "closed")

    def test_conditional_properties(self):
        """Test mapper that adds properties conditionally based on R."""

        class ConditionalMapper(HwlocMapper):
            def finalize_properties(self, properties, R, extra_properties=None):
                # Add TasksMax only if DeviceAllow property is present (GPUs allocated)
                if "DeviceAllow" in properties:
                    properties["TasksMax"] = "1"
                # Call super to get default DevicePolicy behavior
                return super().finalize_properties(
                    properties, R, extra_properties=extra_properties
                )

        mapper = ConditionalMapper(HWLOC_XML)

        # With GPUs - property added
        fake_devs = {"0000:01:00.0": ["/dev/dri/renderD128 rw"]}
        with patch.object(
            mapper,
            "_discover_gpu_devices",
            side_effect=lambda addr: fake_devs.get(addr, []),
        ):
            result = mapper.map(make_R(cores="0", gpus="0"))
        self.assertEqual(result.get("TasksMax"), "1")
        self.assertIn("DeviceAllow", result)
        self.assertEqual(result["DevicePolicy"], "closed")

        # Without GPUs - property not added
        result = mapper.map(make_R(cores="0"))
        self.assertNotIn("TasksMax", result)
        self.assertEqual(result["DevicePolicy"], "closed")

    def test_remove_properties(self):
        """Test mapper that removes properties."""

        class FilterMapper(HwlocMapper):
            def finalize_properties(self, properties, R, extra_properties=None):
                # Remove memory constraints
                properties.pop("AllowedMemoryNodes", None)
                return super().finalize_properties(
                    properties, R, extra_properties=extra_properties
                )

        mapper = FilterMapper(HWLOC_XML)
        result = mapper.map(make_R(cores="0"))

        # CPU property preserved
        self.assertEqual(result["AllowedCPUs"], "0-1")

        # Memory property removed
        self.assertNotIn("AllowedMemoryNodes", result)

        # DevicePolicy still set by default
        self.assertEqual(result["DevicePolicy"], "closed")

    def test_hwloc_mapper_uses_default(self):
        """Test that HwlocMapper inherits default finalize_properties."""
        mapper = HwlocMapper(HWLOC_XML)
        result = mapper.map(make_R(cores="0"))

        # Standard properties: resource mappings + DevicePolicy
        self.assertEqual(
            len(result), 3
        )  # AllowedCPUs, AllowedMemoryNodes, DevicePolicy
        self.assertIn("AllowedCPUs", result)
        self.assertIn("AllowedMemoryNodes", result)
        self.assertEqual(result["DevicePolicy"], "closed")


class TestMergeAllowedDevices(unittest.TestCase):
    """Test merge_allowed_devices(), which applies the static allowlist to a
    mapper's output independent of the mapper class."""

    DEVLINK = re.compile(r"^\S+ [rwm]{1,3}$")

    def _assert_valid_entries(self, device_allow):
        """Every DeviceAllow entry has exactly one space and non-empty perms."""
        for entry in device_allow.split(","):
            self.assertRegex(entry, self.DEVLINK)

    def test_no_existing_device_allow(self):
        """With no mapper DeviceAllow, the entry comes from the allowlist alone."""
        props = {"AllowedCPUs": "0-1", "DevicePolicy": "closed"}
        merge_allowed_devices(props, ["/dev/cxi0 rw"])
        self.assertEqual(props["DeviceAllow"], "/dev/cxi0 rw")
        self._assert_valid_entries(props["DeviceAllow"])

    def test_appended_to_existing(self):
        """Allowlist entries are appended to a mapper-set DeviceAllow value."""
        props = {"DeviceAllow": "/dev/dri/renderD128 rw", "DevicePolicy": "closed"}
        merge_allowed_devices(props, ["/dev/cxi0 rw"])
        entries = props["DeviceAllow"].split(",")
        self.assertIn("/dev/dri/renderD128 rw", entries)
        self.assertIn("/dev/cxi0 rw", entries)
        self._assert_valid_entries(props["DeviceAllow"])

    def test_duplicate_appears_once(self):
        """An allowlist entry duplicating a mapper device is deduplicated."""
        props = {"DeviceAllow": "/dev/dri/renderD128 rw"}
        merge_allowed_devices(props, ["/dev/dri/renderD128 rw"])
        entries = props["DeviceAllow"].split(",")
        self.assertEqual(entries.count("/dev/dri/renderD128 rw"), 1)

    def test_empty_properties_stays_empty(self):
        """Empty properties (unconstrained execution) are not turned on."""
        props = {}
        merge_allowed_devices(props, ["/dev/cxi0 rw"])
        self.assertEqual(props, {})

    def test_empty_allowlist_is_noop(self):
        """An empty or None allowlist leaves properties unchanged."""
        base = {"AllowedCPUs": "0-1", "DevicePolicy": "closed"}
        for allowed in (None, []):
            props = dict(base)
            merge_allowed_devices(props, allowed)
            self.assertEqual(props, base)
            self.assertNotIn("DeviceAllow", props)

    def test_returns_same_dict(self):
        """merge_allowed_devices modifies and returns the same dict."""
        props = {"DevicePolicy": "closed"}
        self.assertIs(merge_allowed_devices(props, ["/dev/cxi0 rw"]), props)

    def test_applies_to_any_mapper_output(self):
        """The merge applies to a custom mapper that never calls super().__init__."""

        class BespokeMapper(ResourceMapper):
            def __init__(self):  # deliberately ignores the base constructor
                self._rank = 0

            def map_cores(self, cores):
                return {"AllowedCPUs": cores}

        result = BespokeMapper().map(make_R(cores="0"))
        merge_allowed_devices(result, ["/dev/cxi0 rw"])
        self.assertEqual(result["DeviceAllow"], "/dev/cxi0 rw")


class TestParseSize(unittest.TestCase):
    """Test the _parse_size() helper."""

    def setUp(self):
        import flux.sdexec.map as m

        self.parse = m._parse_size

    def test_bytes(self):
        value, is_pct = self.parse("1024")
        self.assertAlmostEqual(value, 1024)
        self.assertFalse(is_pct)

    def test_suffix_K(self):
        value, is_pct = self.parse("4K")
        self.assertAlmostEqual(value, 4 * 1024)
        self.assertFalse(is_pct)

    def test_suffix_M(self):
        value, is_pct = self.parse("512M")
        self.assertAlmostEqual(value, 512 * 1024**2)
        self.assertFalse(is_pct)

    def test_suffix_G(self):
        value, is_pct = self.parse("8G")
        self.assertAlmostEqual(value, 8 * 1024**3)
        self.assertFalse(is_pct)

    def test_suffix_lowercase(self):
        value, is_pct = self.parse("4g")
        self.assertAlmostEqual(value, 4 * 1024**3)
        self.assertFalse(is_pct)

    def test_percent(self):
        value, is_pct = self.parse("50%")
        self.assertAlmostEqual(value, 50.0)
        self.assertTrue(is_pct)

    def test_percent_fractional(self):
        value, is_pct = self.parse("33.3%")
        self.assertAlmostEqual(value, 33.3)
        self.assertTrue(is_pct)

    def test_infinity(self):
        value, is_pct = self.parse("infinity")
        self.assertTrue(math.isinf(value))
        self.assertFalse(is_pct)

    def test_infinity_uppercase(self):
        value, is_pct = self.parse("INFINITY")
        self.assertTrue(math.isinf(value))

    def test_invalid_raises(self):
        with self.assertRaises(ValueError):
            self.parse("not-a-size")

    def test_invalid_percent_raises(self):
        with self.assertRaises(ValueError):
            self.parse("x%")


class TestHwlocMapperMemoryMax(unittest.TestCase):
    """Test MemoryMax scaling in HwlocMapper.finalize_properties.

    HWLOC_XML topology: 2 logical cores, 2 PUs each = 4 total PUs.
      core 0 -> AllowedCPUs "0-1" (2 PUs)
      core 1 -> AllowedCPUs "2-3" (2 PUs)
      both   -> AllowedCPUs "0-3" (4 PUs)
    """

    def setUp(self):
        self.mapper = HwlocMapper(HWLOC_XML)

    def tearDown(self):
        del self.mapper

    def test_absolute_scaled(self):
        """Absolute MemoryMax is scaled by alloc/total PU ratio."""
        result = self.mapper.map(
            make_R(cores="0"), extra_properties={"MemoryMax": "8G"}
        )
        # 2 of 4 PUs -> ratio 0.5 -> int(8G * 0.5)
        self.assertEqual(result["MemoryMax"], str(int(8 * 1024**3 * 0.5)))

    def test_percent_scaled_and_rounded(self):
        """Percentage MemoryMax is scaled and rounded to nearest integer percent."""
        result = self.mapper.map(
            make_R(cores="0"), extra_properties={"MemoryMax": "95%"}
        )
        # round(95 * 2/4) = round(47.5) = 48 — exercises rounding
        self.assertEqual(result["MemoryMax"], "48%")

    def test_all_cores_full_value(self):
        """MemoryMax equals the full configured value when all cores allocated."""
        result = self.mapper.map(
            make_R(cores="0-1"), extra_properties={"MemoryMax": "8G"}
        )
        # 4 of 4 PUs -> ratio 1.0
        self.assertEqual(result["MemoryMax"], str(int(8 * 1024**3)))

    def test_no_extra_properties(self):
        """MemoryMax is not set when extra_properties is None."""
        result = self.mapper.map(make_R(cores="0"))
        self.assertNotIn("MemoryMax", result)

    def test_missing_memory_max_key(self):
        """MemoryMax is not set when key is absent from extra_properties."""
        result = self.mapper.map(
            make_R(cores="0"), extra_properties={"CPUAccounting": "true"}
        )
        self.assertNotIn("MemoryMax", result)

    def test_unparsable_value_ignored(self):
        """Unparsable MemoryMax value is silently ignored."""
        result = self.mapper.map(
            make_R(cores="0"), extra_properties={"MemoryMax": "not-a-size"}
        )
        self.assertNotIn("MemoryMax", result)

    def test_infinity_not_scaled(self):
        """infinity MemoryMax is not returned; caller-set value remains."""
        result = self.mapper.map(
            make_R(cores="0"), extra_properties={"MemoryMax": "infinity"}
        )
        self.assertNotIn("MemoryMax", result)

    def test_no_allowed_cpus_no_scaling(self):
        """MemoryMax is not set when AllowedCPUs is absent from properties."""

        class NoCoresMapper(HwlocMapper):
            def map_cores(self, cores):
                return {}

        mapper = NoCoresMapper(HWLOC_XML)
        result = mapper.map(make_R(cores="0"), extra_properties={"MemoryMax": "8G"})
        self.assertNotIn("MemoryMax", result)

    def test_plain_bytes_scaled(self):
        """Plain byte count MemoryMax (no suffix) is scaled by alloc/total PU ratio."""
        result = self.mapper.map(
            make_R(cores="0"), extra_properties={"MemoryMax": "4096"}
        )
        # 2 of 4 PUs -> int(4096 * 0.5) = 2048
        self.assertEqual(result["MemoryMax"], "2048")

    def test_multiple_props_scaled(self):
        """MemoryHigh, MemoryMax, and MemorySwapMax are all scaled in one call."""
        result = self.mapper.map(
            make_R(cores="0"),
            extra_properties={
                "MemoryMax": "8G",
                "MemoryHigh": "6G",
                "MemorySwapMax": "4G",
            },
        )
        # 2 of 4 PUs -> ratio 0.5
        self.assertEqual(result["MemoryMax"], str(int(8 * 1024**3 * 0.5)))
        self.assertEqual(result["MemoryHigh"], str(int(6 * 1024**3 * 0.5)))
        self.assertEqual(result["MemorySwapMax"], str(int(4 * 1024**3 * 0.5)))

    def test_protection_props_not_scaled(self):
        """MemoryMin and MemoryLow are not scaled (they are protection, not cap, properties)."""
        result = self.mapper.map(
            make_R(cores="0"),
            extra_properties={"MemoryMin": "10%", "MemoryLow": "20%"},
        )
        self.assertNotIn("MemoryMin", result)
        self.assertNotIn("MemoryLow", result)

    def test_extra_properties_forwarded_to_finalize(self):
        """ResourceMapper.map() passes extra_properties to finalize_properties."""
        received = {}

        class CapturingMapper(ResourceMapper):
            def map_cores(self, cores):
                return {"AllowedCPUs": cores}

            def finalize_properties(self, properties, R, extra_properties=None):
                received["extra"] = extra_properties
                return properties

        ep = {"MemoryMax": "4G"}
        CapturingMapper().map(make_R(cores="0"), extra_properties=ep)
        self.assertEqual(received["extra"], ep)


class _FakeDev:
    """Describe a fake /dev entry for the expand_allowed_devices tests.

    kind is one of "char", "block", "file", "dir", or "symlink". For a
    symlink, target is the resolved path (a string).
    """

    def __init__(self, kind, target=None):
        self.kind = kind
        self.target = target


class TestExpandAllowedDevices(unittest.TestCase):
    """Test expand_allowed_devices() against a patched fake /dev.

    No broker and no real /dev dependency: Path.glob, Path.stat, and
    Path.resolve are patched to describe a synthetic device tree.
    """

    def setUp(self):
        # Map of absolute path string -> _FakeDev describing it.
        self.tree = {}

    def _expand(self, patterns):
        tree = self.tree

        def fake_glob(path_self, pattern):
            prefix = str(path_self)
            matched = []
            for p in tree:
                if not p.startswith(prefix + "/"):
                    continue
                rel = p[len(prefix) + 1 :]
                if Path(rel).match(pattern):
                    matched.append(Path(p))
            return matched

        def fake_stat(path_self):
            entry = tree.get(str(path_self))
            if entry is None:
                raise OSError(errno.ENOENT, "no such fake device")
            if entry.kind == "char":
                mode = stat_mod.S_IFCHR
            elif entry.kind == "block":
                mode = stat_mod.S_IFBLK
            elif entry.kind == "dir":
                mode = stat_mod.S_IFDIR
            elif entry.kind == "symlink":
                # stat() follows the link to its target
                target = tree.get(entry.target)
                if target is None:
                    raise OSError(errno.ENOENT, "dangling symlink")
                mode = stat_mod.S_IFCHR if target.kind == "char" else stat_mod.S_IFBLK
            else:
                mode = stat_mod.S_IFREG
            st = mock.Mock()
            st.st_mode = mode
            return st

        def fake_resolve(path_self, strict=False):
            entry = tree.get(str(path_self))
            if entry is not None and entry.kind == "symlink":
                return Path(entry.target)
            return path_self

        with patch.object(Path, "glob", fake_glob), patch.object(
            Path, "stat", fake_stat
        ), patch.object(Path, "resolve", fake_resolve):
            return m.expand_allowed_devices(patterns)

    def test_single_literal(self):
        self.tree = {"/dev/cxi0": _FakeDev("char")}
        self.assertEqual(self._expand(["/dev/cxi0"]), ["/dev/cxi0 rw"])

    def test_glob_multiple_sorted(self):
        self.tree = {
            "/dev/cxi1": _FakeDev("char"),
            "/dev/cxi0": _FakeDev("char"),
        }
        self.assertEqual(self._expand(["/dev/cxi*"]), ["/dev/cxi0 rw", "/dev/cxi1 rw"])

    def test_explicit_perms(self):
        self.tree = {"/dev/cxi0": _FakeDev("char")}
        self.assertEqual(self._expand(["/dev/cxi* r"]), ["/dev/cxi0 r"])

    def test_perms_separated_by_any_whitespace(self):
        # The pattern and perms may be separated by any whitespace run, not
        # just a single space.
        self.tree = {"/dev/cxi0": _FakeDev("char")}
        self.assertEqual(self._expand(["/dev/cxi0\tr"]), ["/dev/cxi0 r"])
        self.assertEqual(self._expand(["/dev/cxi0   rw"]), ["/dev/cxi0 rw"])

    def test_invalid_perms_raises(self):
        with self.assertRaises(ValueError):
            self._expand(["/dev/foo x"])
        with self.assertRaises(ValueError):
            self._expand(["/dev/foo rwmx"])

    def test_too_many_fields_raises(self):
        with self.assertRaises(ValueError):
            self._expand(["/dev/foo rw extra"])

    def test_empty_entry_raises(self):
        with self.assertRaises(ValueError):
            self._expand(["   "])

    def test_pattern_not_under_dev_raises(self):
        for bad in ["/etc/*", "dev/cxi0", "/dev"]:
            with self.assertRaises(ValueError):
                self._expand([bad])

    def test_pattern_with_dotdot_raises(self):
        with self.assertRaises(ValueError):
            self._expand(["/dev/../etc/shadow"])

    def test_recursive_glob_rejected(self):
        # "**" would recurse into subdirectories of /dev; reject it explicitly.
        for bad in ["/dev/**", "/dev/**/cxi0"]:
            with self.assertRaises(ValueError):
                self._expand([bad])

    def test_non_string_entry_raises(self):
        with self.assertRaises(ValueError):
            self._expand([42])

    def test_regular_file_skipped(self):
        self.tree = {"/dev/notadev": _FakeDev("file")}
        self.assertEqual(self._expand(["/dev/notadev"]), [])

    def test_directory_skipped(self):
        self.tree = {"/dev/shm": _FakeDev("dir")}
        self.assertEqual(self._expand(["/dev/shm"]), [])

    def test_symlink_outside_dev_skipped(self):
        self.tree = {
            "/dev/evil": _FakeDev("symlink", target="/tmp/evil"),
            "/tmp/evil": _FakeDev("char"),
        }
        self.assertEqual(self._expand(["/dev/evil"]), [])

    def test_symlink_inside_dev_returned(self):
        self.tree = {
            "/dev/link": _FakeDev("symlink", target="/dev/cxi0"),
            "/dev/cxi0": _FakeDev("char"),
        }
        self.assertEqual(self._expand(["/dev/link"]), ["/dev/link rw"])

    def test_no_match_silently_ignored(self):
        # A glob matching nothing yields no entries and raises nothing, so a
        # global allowed-devices policy works across heterogeneous nodes.
        self.assertEqual(self._expand(["/dev/nomatch*"]), [])

    def test_duplicate_patterns_deduplicated(self):
        self.tree = {"/dev/cxi0": _FakeDev("char")}
        self.assertEqual(self._expand(["/dev/cxi0", "/dev/cxi0"]), ["/dev/cxi0 rw"])


class TestMain(unittest.TestCase):
    """Test the main() CLI entry point."""

    def _run_main(self, args, xml=HWLOC_XML):
        """Run main() with patched topology XML; return parsed JSON output."""
        captured = io.StringIO()
        with patch.object(m, "_get_system_xml", return_value=xml), patch(
            "sys.stdout", captured
        ):
            ret = m.main(args)
        self.assertEqual(ret, 0)
        return json.loads(captured.getvalue())

    def test_cores_only(self):
        result = self._run_main(["--cores=0"])
        self.assertEqual(result["AllowedCPUs"], "0-1")
        self.assertEqual(result["AllowedMemoryNodes"], "0")
        self.assertEqual(result["DevicePolicy"], "closed")

    def test_cores_all(self):
        result = self._run_main(["--cores=0-1"])
        self.assertEqual(result["AllowedCPUs"], "0-3")
        self.assertEqual(result["AllowedMemoryNodes"], "0")

    def test_cores_and_gpus(self):
        fake_devs = {"0000:01:00.0": ["/dev/dri/renderD128 rw"]}
        captured = io.StringIO()
        with patch.object(m, "_get_system_xml", return_value=HWLOC_XML), patch.object(
            m.HwlocMapper,
            "_discover_gpu_devices",
            autospec=True,
            side_effect=lambda self, addr: fake_devs.get(addr, []),
        ), patch("sys.stdout", captured):
            ret = m.main(["--cores=0", "--gpus=0"])
        self.assertEqual(ret, 0)
        result = json.loads(captured.getvalue())
        self.assertEqual(result["AllowedCPUs"], "0-1")
        self.assertIn("/dev/dri/renderD128 rw", result["DeviceAllow"])
        self.assertEqual(result["DevicePolicy"], "closed")

    def test_xml_file(self):
        with tempfile.NamedTemporaryFile(mode="w", suffix=".xml", delete=False) as fh:
            fh.write(HWLOC_XML)
            fname = fh.name
        try:
            captured = io.StringIO()
            with patch("sys.stdout", captured):
                ret = m.main(["--xml", fname, "--cores=0"])
            self.assertEqual(ret, 0)
            result = json.loads(captured.getvalue())
            self.assertEqual(result["AllowedCPUs"], "0-1")
        finally:
            os.unlink(fname)

    def test_error_returns_nonzero(self):
        with patch.object(m, "_get_system_xml", return_value=HWLOC_XML), patch.object(
            m.HwlocMapper, "map", side_effect=OSError("mock failure")
        ):
            ret = m.main(["--cores=0"])
        self.assertEqual(ret, 1)

    def test_allowed_devices_merged(self):
        """--allowed-devices entries appear in DeviceAllow."""
        captured = io.StringIO()
        with patch.object(m, "_get_system_xml", return_value=HWLOC_XML), patch.object(
            m, "expand_allowed_devices", return_value=["/dev/null rw"]
        ), patch("sys.stdout", captured):
            ret = m.main(["--cores=0", "--allowed-devices=/dev/null"])
        self.assertEqual(ret, 0)
        result = json.loads(captured.getvalue())
        self.assertEqual(result["DeviceAllow"], "/dev/null rw")
        self.assertEqual(result["DevicePolicy"], "closed")

    def test_allowed_devices_bad_pattern_exits(self):
        """A malformed --allowed-devices pattern raises SystemExit."""
        with patch.object(m, "_get_system_xml", return_value=HWLOC_XML):
            with self.assertRaises(SystemExit):
                m.main(["--cores=0", "--allowed-devices=/etc/passwd"])


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())


# vi: ts=4 sw=4 expandtab
