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

import json
import unittest
from unittest.mock import patch

import subflux  # noqa: F401 - for PYTHONPATH
from flux.sdexec.map import HwlocMapper, ResourceMapper
from pycotap import TAPTestRunner


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


class TestCustomGpuMapper(unittest.TestCase):
    """Subclass overriding map_gpus; cores mapping is inherited from HwlocMapper."""

    def setUp(self):
        class NvidiaMapper(HwlocMapper):
            def map_gpus(self, gpus):
                # Custom implementation returning vendor-specific device paths.
                from flux.idset import IDset

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
            def finalize_properties(self, properties, R):
                properties.update(
                    {
                        "CPUAccounting": "true",
                        "MemoryAccounting": "true",
                    }
                )
                return super().finalize_properties(properties, R)

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
            def finalize_properties(self, properties, R):
                # Override CPU allocation to a fixed value
                properties["AllowedCPUs"] = "0"
                return super().finalize_properties(properties, R)

        mapper = OverrideMapper(HWLOC_XML)
        result = mapper.map(make_R(cores="0-1"))

        # Property was overridden
        self.assertEqual(result["AllowedCPUs"], "0")
        self.assertEqual(result["DevicePolicy"], "closed")

    def test_conditional_properties(self):
        """Test mapper that adds properties conditionally based on R."""

        class ConditionalMapper(HwlocMapper):
            def finalize_properties(self, properties, R):
                # Add TasksMax only if DeviceAllow property is present (GPUs allocated)
                if "DeviceAllow" in properties:
                    properties["TasksMax"] = "1"
                # Call super to get default DevicePolicy behavior
                return super().finalize_properties(properties, R)

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
            def finalize_properties(self, properties, R):
                # Remove memory constraints
                properties.pop("AllowedMemoryNodes", None)
                return super().finalize_properties(properties, R)

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


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())


# vi: ts=4 sw=4 expandtab
